Simple AF Converter (SAFC)
==========================

## MinGW branch

No description 

How to build:
Compiler and linker flags are listed in subsequent files in `./_SAFC_` + do not forget to set `-std=c++17`  
You'd need to find and put freeglut (glut32 is unnecessary, but it's listed in includes) for MinGW somewhere.  

Bugs:
I think that there are tons of memory leaks in that, yet it works. Might be helpful in case if somebody has problems with original SAFC's GUI.

