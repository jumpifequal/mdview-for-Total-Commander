[CmdletBinding()]
param(
    [ValidateSet('Publish', 'Check', 'Package')]
    [string] $Action = 'Check',
    [switch] $IncludeXP,
    [string] $XPToolchainRoot
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
$remote = 'https://github.com/jumpifequal/mdview-for-Total-Commander.git'

function Get-SourceVersion([string] $Header) {
    $version = [regex]::Match((Get-Content -Raw -LiteralPath $Header),
        '(?m)^#define\s+(?:MDVIEW_VERSION|VER_FILEVERSION_STR)\s+"(\d+\.\d+\.\d+)(?:\\0)?"\s*$')
    if (!$version.Success) { throw "Cannot read the source version from $Header." }
    return [version] $version.Groups[1].Value
}

function Invoke-Git([string[]] $Arguments, [string] $Step) {
    & git --no-pager @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed (Git exit code $LASTEXITCODE). See the Git message above."
    }
}

function Confirm-PublishedCommit([string] $Checkout) {
    $commit = Invoke-Git @('-C', $Checkout, 'rev-parse', 'HEAD') 'Read release commit'
    $branch = Invoke-Git @('-C', $Checkout, 'ls-remote', '--exit-code', 'origin', 'refs/heads/main') 'Verify pushed branch'
    if (($branch -split '\s+')[0] -ne $commit) {
        Invoke-Git @('-C', $Checkout, 'fetch', 'origin', 'refs/heads/main') 'Fetch updated main' | Out-Host
        Invoke-Git @('-C', $Checkout, 'merge-base', '--is-ancestor', $commit, 'FETCH_HEAD') 'Verify release commit on main'
    }
    return $commit
}

function Get-MSBuild {
    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $candidate = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
        if (!$candidate) {
            $candidate = & $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
        }
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    throw 'MSBuild was not found. Install the Visual Studio C++ build tools or run from the corresponding Developer Command Prompt.'
}

function Get-XPBuild([string] $MSBuild, [string] $ToolchainRoot) {
    if ($ToolchainRoot) {
        $root = (Resolve-Path -LiteralPath $ToolchainRoot).ProviderPath.Replace('\', '/')
        $instance = (Get-Item -LiteralPath $MSBuild).Directory.Parent.Parent.Parent.FullName.Replace('\', '/')
        $compiler = "$instance/VC/Tools/MSVC/14.16.27023"
        $ucrt = "$root/ucrt"
        foreach ($file in @(
            "$compiler/bin/HostX86/x86/cl.exe",
            "$root/targets/Platforms/Win32/PlatformToolsets/v141_xp/Toolset.props",
            "$root/targets/Platforms/Win32/PlatformToolsets/v141_xp/Toolset.targets",
            "$root/sdk/Include/Windows.h", "$root/sdk/Lib/Kernel32.Lib",
            "$ucrt/Include/10.0.10240.0/ucrt/stdio.h", "$ucrt/Lib/10.0.10240.0/ucrt/x86/libucrt.lib"
        )) {
            if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing XP build file: $file" }
        }
        return @{
            MSBuild = $MSBuild
            Properties = "VCTargetsPath=$root/targets/;VCToolsInstallDir=$compiler/;WindowsSdkDir_71A=$root/sdk/;UniversalCRT_IncludePath=$ucrt/Include/10.0.10240.0/ucrt;UniversalCRT_LibraryPath_x86=$ucrt/Lib/10.0.10240.0/ucrt/x86"
        }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $candidate = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.WinXP -find 'MSBuild\Current\Bin\MSBuild.exe' |
            Select-Object -First 1
        if ($candidate) { $MSBuild = $candidate }
    }
    return @{ MSBuild = $MSBuild; Properties = '' }
}

function Assert-XPImage([string] $Binary) {
    $image = [IO.File]::ReadAllBytes($Binary)
    if ($image.Length -lt 128 -or [BitConverter]::ToUInt16($image, 0) -ne 0x5a4d) {
        throw "$Binary is not a PE image."
    }
    $pe = [BitConverter]::ToInt32($image, 60)
    if ($pe -lt 0 -or $pe -gt ($image.Length - 94) -or [BitConverter]::ToUInt32($image, $pe) -ne 0x4550) {
        throw "$Binary has an invalid PE header."
    }
    $optional = $pe + 24
    if ([BitConverter]::ToUInt16($image, $pe + 4) -ne 0x14c -or
        !([BitConverter]::ToUInt16($image, $pe + 22) -band 0x2000) -or
        [BitConverter]::ToUInt16($image, $optional) -ne 0x10b -or
        [BitConverter]::ToUInt16($image, $optional + 40) -ne 5 -or
        [BitConverter]::ToUInt16($image, $optional + 42) -ne 1 -or
        [BitConverter]::ToUInt16($image, $optional + 48) -ne 5 -or
        [BitConverter]::ToUInt16($image, $optional + 50) -ne 1 -or
        [BitConverter]::ToUInt16($image, $optional + 68) -ne 2) {
        throw "$Binary must be a 32-bit Windows DLL with OS and subsystem versions set to 5.01."
    }
}

function Build-Plugin([string] $MSBuild, [string] $Configuration, [string] $Platform,
                     [string] $Binary, [string] $Work, [version] $Version,
                     [string] $ExtraProperties) {
    $output = Join-Path $Work "$Configuration-$Platform"
    $msbuildPath = $output.Replace('\', '/')
    $properties = "Configuration=$Configuration;Platform=$Platform;OutDir=$msbuildPath/;IntDir=$msbuildPath/obj/"
    if ($ExtraProperties) { $properties += ";$ExtraProperties" }
    & $MSBuild (Join-Path $project 'mdview.vcxproj') /nologo /nr:false /v:minimal "/p:$properties" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $Configuration|$Platform (exit code $LASTEXITCODE)." }
    $path = Join-Path $output $Binary
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "The build did not produce $path." }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
    $builtVersion = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart)
    if ($builtVersion -ne $Version) { throw "$Binary has version $builtVersion but expected $Version." }
    if ($Configuration -eq 'ReleaseXP') { Assert-XPImage $path }
    return $path
}

function New-PluginArchive([string] $Work, [string] $Name, [string[]] $Binaries, [switch] $XP) {
    $layout = New-Item -Path (Join-Path $Work $Name) -ItemType Directory
    foreach ($file in @('README.md', 'CHANGELOG.txt', 'LICENSE', 'pluginst.inf')) {
        Copy-Item -LiteralPath (Join-Path $project $file) -Destination $layout.FullName
    }
    foreach ($folder in @('Themes', 'Screenshots')) {
        $dest = New-Item -Path (Join-Path $layout.FullName $folder) -ItemType Directory
        $filter = if ($folder -eq 'Themes') { '*.json' } else { '*.png' }
        foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $project $folder) -File -Filter $filter)) {
            Copy-Item -LiteralPath $file.FullName -Destination $dest.FullName
        }
    }
    $samples = New-Item -Path (Join-Path $layout.FullName 'Sample_md_files') -ItemType Directory
    foreach ($file in $sampleFiles) {
        Copy-Item -LiteralPath (Join-Path $project $file) -Destination $samples.FullName
    }
    foreach ($binary in $Binaries) {
        Copy-Item -LiteralPath $binary -Destination $layout.FullName
    }
    if ($XP) {
        $ini = Join-Path $layout.FullName 'pluginst.inf'
        $source = Get-Content -Raw -LiteralPath $ini
        $source = $source -replace '(?m)^(description=[^\r\n]+)(?=\r?$)', '$1 (Windows XP)'
        $source = $source -replace '(?m)^file=mdview\.wlx(?=\r?$)', 'file=mdview_xp.wlx'
        [IO.File]::WriteAllText($ini, $source, [Text.Encoding]::ASCII)
    }
    $archive = Join-Path $Work "$Name.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($layout.FullName, $archive)
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entryNames = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        $required = @('LICENSE', 'pluginst.inf', 'Themes/github-light.json', 'Themes/github-dark.json')
        $required += @($Binaries | ForEach-Object { Split-Path -Leaf $_ })
        foreach ($name in $required) {
            if ($entryNames -notcontains $name) { throw "Package $archive is missing $name." }
        }
        if ($XP -and $entryNames -contains 'mdview.wlx64') { throw 'The XP package contains a modern binary.' }
    } finally {
        $zip.Dispose()
    }
    return $archive
}

function Remove-WorkDirectory([string] $Work) {
    $item = Get-Item -LiteralPath $Work -Force -ErrorAction SilentlyContinue
    if (!$item) { return }
    $resolved = (Resolve-Path -LiteralPath $Work).ProviderPath
    if ($resolved -ne $Work -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        Write-Warning "Temporary directory needs manual cleanup: $Work"
        return
    }
    if (Get-ChildItem -LiteralPath $resolved -Force -Recurse -Attributes ReparsePoint) {
        Write-Warning "Temporary directory contains reparse points: $Work"
        return
    }
    Remove-Item -LiteralPath $resolved -Force -Recurse
}

$work = $null
try {
    if ($IncludeXP -and $Action -ne 'Package') { throw '-IncludeXP applies only to Package.' }
    if ($XPToolchainRoot -and !$IncludeXP) { throw '-XPToolchainRoot requires -IncludeXP.' }
    $version = Get-SourceVersion (Join-Path $project 'resource.h')
    $sampleFiles = @(
        'Sample_md_files/markdown_en.md', 'Sample_md_files/readme_problematic.md',
        'Sample_md_files/test.md', 'Sample_md_files/test_mermaid.md',
        'Sample_md_files/test_mermaid_stress.md', 'Sample_md_files/Selectable mathematics.md'
    )
    $installer = Get-Content -Raw -LiteralPath (Join-Path $project 'pluginst.inf')
    if (!$installer.Contains("v$version") -or $installer -notmatch '(?m)^file=mdview\.wlx\s*$') {
        throw "pluginst.inf does not identify MDView $version and mdview.wlx."
    }
    if ((Get-Content -LiteralPath (Join-Path $project 'CHANGELOG.txt') -TotalCount 1) -ne "v$version") {
        throw "CHANGELOG.txt must start with v$version."
    }
    $work = Join-Path $project ".mdview-release-$([guid]::NewGuid().ToString('N'))"
    New-Item -Path $work -ItemType Directory | Out-Null

    if ($Action -eq 'Package') {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $msbuild = Get-MSBuild
        if ($IncludeXP) {
            $xpBuild = Get-XPBuild $msbuild $XPToolchainRoot
            $xp = Build-Plugin $xpBuild.MSBuild ReleaseXP Win32 'mdview_xp.wlx' $work $version $xpBuild.Properties
        }
        $x86 = Build-Plugin $msbuild Release Win32 'mdview.wlx' $work $version
        $x64 = Build-Plugin $msbuild Release x64 'mdview.wlx64' $work $version
        $archives = @(New-PluginArchive $work "WLX_mdview_TC-plugin_v$version" @($x86, $x64))
        if ($IncludeXP) {
            $archives += New-PluginArchive $work "WLX_mdviewXP_TC-plugin_v$version" @($xp) -XP
        }
        $destination = Join-Path $project 'Distribution'
        New-Item -Path $destination -ItemType Directory -Force | Out-Null
        foreach ($archive in $archives) {
            $target = Join-Path $destination (Split-Path -Leaf $archive)
            Copy-Item -LiteralPath $archive -Destination $target -Force
            Write-Host "Prepared $target"
        }
    } else {
        if (!(Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git for Windows is required.' }
        $checkout = Join-Path $work 'checkout'
        Write-Host "Reading GitHub main for MDView $version..."
        Invoke-Git @('clone', '--single-branch', '--branch', 'main', '--', $remote, $checkout) 'Clone GitHub main'
        $publishedVersion = Get-SourceVersion (Join-Path $checkout 'resource.h')
        $sourceFiles = @(
            '.gitignore', 'LICENSE', 'README.md', 'CHANGELOG.txt',
            'mdview.c', 'mdview.def', 'mdview.sln', 'mdview.vcxproj', 'mdview.vcxproj.filters',
            'mdview_autolink.inc', 'mdview_encoding.inc', 'mdview_highlight.inc', 'mdview_math.inc', 'mdview_theme.inc',
            'resource.h', 'resource.rc', 'pluginst.inf',
            'build_x32.bat', 'build_x64.bat', 'publishme.bat', 'tools/mdview-release.ps1'
        ) + $sampleFiles
        foreach ($folder in @('Themes', 'Screenshots')) {
            $filter = if ($folder -eq 'Themes') { '*.json' } else { '*.png' }
            $sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $project $folder) -File -Filter $filter |
                Sort-Object Name | ForEach-Object { "$folder/$($_.Name)" })
        }
        foreach ($file in $sourceFiles) {
            if (!(Test-Path -LiteralPath (Join-Path $project $file) -PathType Leaf)) { throw "Missing release file: $file" }
            $target = Join-Path $checkout $file
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $project $file) -Destination $target
        }
        Invoke-Git (@('-C', $checkout, '-c', 'core.safecrlf=false', 'add', '--') + $sourceFiles) 'Stage release files'
        Invoke-Git @('-C', $checkout, '-c', 'core.whitespace=cr-at-eol', 'diff', '--cached', '--check') 'Check staged release files'
        & git --no-pager -C $checkout diff --cached --quiet
        $status = $LASTEXITCODE
        if ($status -eq 0) {
            Write-Host "GitHub main already contains the selected MDView files."
        } elseif ($status -eq 1) {
            if ($publishedVersion -ge $version) {
                throw "GitHub main is already at $publishedVersion and has different source files. Review those changes and choose a new release version."
            }
            Invoke-Git @('-C', $checkout, 'diff', '--cached', '--stat') 'Show staged release files'
            if ($Action -eq 'Publish') {
                Write-Host "Creating the MDView $version release commit..."
                Invoke-Git @('-C', $checkout, 'commit', '-m', "Release MDView $version") 'Create release commit'
                Write-Host 'Pushing to GitHub main. Complete any Git Credential Manager sign-in prompt.'
                Invoke-Git @('-C', $checkout, 'push', 'origin', 'HEAD:refs/heads/main') 'Push release commit to GitHub main'
                $commit = Confirm-PublishedCommit $checkout
                Write-Host "Verified MDView $version on GitHub main."
                Write-Host "https://github.com/jumpifequal/mdview-for-Total-Commander/commit/$commit"
            } else {
                Write-Host "MDView $version source is ready. No commit or push was made."
            }
        } else { throw "Git could not compare the staged files (exit code $status)." }
    }
} catch {
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
} finally {
    if ($work) {
        try { Remove-WorkDirectory $work }
        catch { Write-Warning "Could not remove temporary directory $work. $($_.Exception.Message)" }
    }
}
