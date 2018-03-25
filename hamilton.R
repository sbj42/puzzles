# -*- makefile -*-

hamilton : [X] GTK COMMON hamilton hamilton-icon|no-icon
hamilton : [G] WINDOWS COMMON hamilton hamilton.res|noicon.res

ALL += hamilton[COMBINED]

!begin am gtk
GAMES += hamilton
!end

!begin >list.c
    A(hamilton) \
!end

!begin >gamedesc.txt
hamilton:hamilton.exe:Hamilton:Path-building puzzle:Connect the squares into a single path.
!end