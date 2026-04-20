@if not exist Win32\Release mkdir Win32\Release
cl /LD /O1 /GL /DNDEBUG mdview.c mdview.def ^
  ole32.lib oleaut32.lib uuid.lib advapi32.lib user32.lib gdi32.lib shell32.lib ^
  /link /OUT:Win32\Release\mdview.wlx /IMPLIB:Win32\Release\mdview.lib /LTCG
