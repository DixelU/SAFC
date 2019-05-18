Simple AF Converter (SAFC)
==========================

Is a first application in *SAF* branch apps. *Specifically for Black MIDIS*

SAFC is a converter which can merge midis with different **PPQ**s and specify some unusual ones (like 17 or 42);
override tempo, override instruments of midis, fix headers (if possible) and events (if possible). And all that SAFC does *without loading midi in memory. Wow!* 

If you're still not amazed, The Launcher made on pure C++ without any frameworks. ***It's a console app which you can control with mouse!***

There's also discord server, where SAFC was publishing all that time since July/August of 2018: https://discord.gg/CsgEW4P

Includes:
* SAFC.exe (executable which doing all the work) *(works like a compilier, taking a ton of arguments)*
* Launcher.exe *(You can get what it does from the exectuable's name :D)*
* Sea.exe and glut32.dll **~Sea launcher**
* *~~OCRer~~* **(Search for updated one, located in repository called SAFOR)**

Do not look into the sources of SAF apps. Inside they're a bunch of quite weird functions and global variables.
But that's just a first attempts, don't blame me for that :D

Sea Launcher for SAFC
============================

Less impressive but yet not worse! 
It was written with using deprecated version of GLUT to keep compatibility with older windows versions. 
Hard use of object-oriented programming made it a lot easier to add new functions to it! 

It has the same functionality as default console launcher.
