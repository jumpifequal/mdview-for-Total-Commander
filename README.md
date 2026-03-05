# MDView

**A Markdown viewer plugin for Total Commander.**

Press F3 on any `.md` file and get a clean, fully rendered preview — dark mode, syntax highlighting, table of contents, find-in-page, split source view, and more. Single C file, zero dependencies, ~130 KB.

![License](https://img.shields.io/badge/license-MIT-blue.svg)

---

## Screenshots

> *TODO: Add screenshots of light mode, dark mode, split view, and the help overlay.*

## Features

- **Full Markdown rendering** — headings, bold, italic, strikethrough, links, images, tables with column alignment, fenced and indented code blocks, blockquotes (nested), ordered/unordered/task lists, horizontal rules, autolinks, and escape sequences
- **Reference-style links and images** — `[text][label]` and `![alt][label]` with `[label]: URL "title"` definitions
- **Embedded HTML** — raw HTML blocks (`<div>`, `<details>`, `<table>`, etc.) and inline HTML tags (`<mark>`, `<kbd>`, `<br>`, etc.) are passed through and rendered natively
- **Local image support** — relative image paths resolve correctly from the markdown file's directory
- **Syntax highlighting** — JavaScript, TypeScript, Python, C, C++, C#, Java, Rust, Go, SQL, Bash, CSS/SCSS, PHP, HTML, and XML
- **Dark / light mode** — toggle with Ctrl+D, or auto-detected from your Windows theme on first launch
- **Split source view** — side-by-side rendered Markdown and raw source with synchronised scrolling (Ctrl+M)
- **Smart clipboard** — Ctrl+C copies formatted HTML from the rendered view, or raw Markdown from the source pane
- **Right-click context menu** — Copy, Select All, Split View, Find, TOC, zoom controls, dark mode toggle, and more
- **Adjustable layout** — zoom in/out, optionally constrain reading column width
- **Line numbers** — toggle on code blocks with Ctrl+L
- **Table of Contents** — auto-generated sidebar from your headings
- **Find in page** — incremental search with match highlighting and navigation
- **Expand / collapse** — long code blocks and blockquotes are collapsed by default with a "Show more" button
- **Persistent settings** — font size, theme, column width, and line numbers are saved and restored between sessions
- **Print support** — Ctrl+P renders a clean printable version
- **Progress bar** — subtle reading position indicator at the top of the viewport
- **Full window resize** — content fills the entire viewport and resizes correctly when maximised or dragged
- **Unicode path support** — CJK and other non-ASCII characters in file paths are handled correctly

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl` `C` | Copy (HTML from rendered, raw text from source) |
| `Ctrl` `A` | Select all |
| `Ctrl` `M` | Toggle split source view |
| `Ctrl` `+` | Zoom in |
| `Ctrl` `-` | Zoom out |
| `Ctrl` `0` | Reset zoom |
| `Ctrl` `W` | Constrain column width |
| `Ctrl` `Shift` `W` | Widen or remove column constraint |
| `Ctrl` `D` | Toggle dark / light mode |
| `Ctrl` `L` | Toggle line numbers |
| `Ctrl` `T` | Table of Contents |
| `Ctrl` `F` | Find in page |
| `Ctrl` `P` | Print |
| `Ctrl` `G` | Go to top |
| `Esc` | Close viewer |
| `F1` | Show shortcut reference |

Press **F1** inside the viewer for an on-screen reference. Right-click anywhere for a context menu with all major actions.

## Download

Grab the latest release from the [Releases](../../releases) page. The zip contains both 32-bit and 64-bit builds.

## Installation

### Automatic

Open the downloaded `.zip` file **inside Total Commander** (navigate to it and press Enter or double-click). The included `pluginst.inf` will trigger TC's automatic plugin installer.

### Manual

1. Extract `mdview.wlx` (32-bit) or `mdview.wlx64` (64-bit) to a directory of your choice
2. In Total Commander: **Configuration → Options → Plugins → Lister (WLX) → Add**
3. Select the `.wlx` / `.wlx64` file
4. The detect string auto-configures for `.md`, `.markdown`, `.mkd`, and `.mkdn` extensions

## Usage

1. Navigate to any Markdown file in Total Commander
2. Press **F3** to open the lister
3. Press **Ctrl+M** to toggle the split source view
4. Use keyboard shortcuts or right-click for a context menu — your preferences are saved automatically

## Building from Source

The entire plugin is a single C file. Cross-compile from Linux with MinGW, or build natively on Windows with any GCC or MSVC toolchain.

```bash
# 32-bit
i686-w64-mingw32-gcc -shared -o mdview.wlx mdview.c mdview.def \
    -lole32 -loleaut32 -luuid -ladvapi32 -lgdi32 -O2 -s -static-libgcc

# 64-bit
x86_64-w64-mingw32-gcc -shared -o mdview.wlx64 mdview.c mdview.def \
    -lole32 -loleaut32 -luuid -ladvapi32 -lgdi32 -O2 -s -static-libgcc
```

No external libraries or build systems required.

## WLXHarness (Test Tool)

The `WLXHarness/` directory contains a standalone test harness (contributed by Nigurrath) that loads any WLX plugin outside of Total Commander. It creates a host window, calls `ListLoadW`, and forwards resize events — useful for rapid development without restarting TC. A pre-built `WLXHarness.exe` is included.

## How It Works

MDView is a WLX lister plugin — a DLL that Total Commander loads when you press F3 on a matching file type. It contains a built-in Markdown-to-HTML converter and embeds an MSHTML (IE11) WebBrowser control to render the output. The rendered HTML is written to a temporary file in the same directory as the source `.md` file, allowing local relative image paths to resolve correctly via the Local Machine security zone. The split source view uses a Windows RichEdit control with synchronised ratio-based scrolling. Keyboard input is handled by subclassing the browser's internal window and (when active) the RichEdit control. Settings are persisted via TC's standard INI file mechanism.

## Credits

Split source view, synchronised scrolling, context-aware clipboard (CF_HTML copy), OLE command helpers, and the WLXHarness test tool were contributed by **Nigurrath** (Senior Ghisler.ch Total Commander Forum member). These features were merged from their v2.2 fork, adapted to maintain MinGW cross-compilation compatibility and combined with improvements developed independently (CJK path support, reference-style links, HTML passthrough, local image rendering).

### Not carried over from Nigurrath's fork

- **`_s` safe functions** (`strncpy_s`, `sscanf_s`, etc.) — these are MSVC-specific and break MinGW cross-compilation. Standard C equivalents are used instead, with equivalent safety via explicit size bounds.
- **Paste command** (`Ctrl+V` / menu item) — MDView is a read-only viewer, not an editor. Paste has no meaningful target.
- **`#endif _UNICODE` trailing text** — the original had compiler-warning-level trailing text after `#endif`; corrected to standard preprocessor form.

## File List

| File | Description |
|---|---|
| `mdview.c` | Complete plugin source (~1830 lines) |
| `mdview.def` | DLL export definitions |
| `pluginst.inf` | Total Commander auto-install manifest |
| `test.md` | Sample document exercising all features |
| `WLXHarness/` | Standalone WLX test harness (contributed by Nigurrath) |

## License

[MIT](LICENSE)
