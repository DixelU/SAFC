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
* *~~OCRer.exe~~* (quite broken app supposed to make OCR'ed midi from the dropped on executable one, but its bugged a little bit but it's still quite connected to SAFC.exe)**(Seek for updated one located in repository called SAFOR)**

Do not look into the sources of SAF apps. Inside they're a bunch of quite weird functions and global variables.
But that's just a first attempts, don't blame me for that :D
