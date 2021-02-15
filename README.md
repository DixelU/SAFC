Simple AF Converter (SAFC)
==========================

**Merging completely different midis by completely different ways!**

SAFC is a converter which can merge midis with different PPQs and specify some unusual ones (like 17 or 42); override tempo, override instruments of midis, fix headers (if possible) and events (if possible). Also does volume and pitch mapping, can cut notes to specific range, and transpose a whole MIDI. Able to cut a part of some midi. + New merge technique which is impossible to make manually in Hex editor: **Inplace merge**

It has a big story. First version was literally compilier-like application and dos-like interactive launcher. Then it got a simple OpenGL launcher, yet having the same old basement in the entity of *SAFC.exe*. Now it's all in the past, and there's now single app with no additional files :)
There's also discord server, where SAF apps were publishing all that time since July/August of 2018: https://discord.gg/CsgEW4P
~~You can download and read manual right here *(it's just a png file, so anyone anywhere can read it)*: [Manual.png](https://github.com/DixelU/SAFC/blob/master/Manual.png) It will get updated **only** on major updates, adding new features.~~  
[Also i'm slowly filling up the wiki, for easier access in comparison to Manual.png](https://github.com/DixelU/SAFC/wiki)

**Noting that Support of Windows XP was dropped since v1.0**
Also thanks to all beta testers on SAF server!  

You can compile it using MSVC 2017 (and later) by installing freeglut and glew using vcpkg/nuget :)
