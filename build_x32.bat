cl /LD /O1 /GL /DNDEBUG mdview.c mdview.def ^
  ole32.lib oleaut32.lib uuid.lib advapi32.lib user32.lib gdi32.lib ^
  /link /OUT:mdview.wlx /LTCG
