@if not exist x64\Release mkdir x64\Release
cl /LD /O1 /GL /Gy /Gw /DNDEBUG mdview.c mdview.def ^
  ole32.lib oleaut32.lib uuid.lib advapi32.lib user32.lib gdi32.lib shell32.lib ^
  /link /OUT:x64\Release\mdview.wlx64 /IMPLIB:x64\Release\mdview.lib /LTCG /OPT:REF /OPT:ICF
