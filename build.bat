windres my.rc -O coff -o my.res
gcc hexedit.c -o ..\hexedit my.res -lpdcurses -lcomdlg32 -Wall -g
