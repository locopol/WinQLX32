WinQLX32
========

minqlx is a modification to the Quake Live Dedicated Server that extends Quake Live's dedicated server with
extra functionality and allows scripting of server behavior through an embedded Python interpreter.

This version is a **port** of original **minqlx** developed by *MinoMino* (https://github.com/MinoMino/minqlx) 
and the enhancements of *tjone270* (https://github.com/tjone270/minqlxtended) to made compatible with Windows 
and official 32 bit version of Steam binaries of **Quake Live** (1069 compilation)

If you have any questions or suggestions, send me a message :-) -> https://www.reddit.com/user/locopole/

* **Code is in Beta stage, include the python core routines based on MinoMino Repo with minimal refactor 
to mantain full compatibility**

Installation
============

- To make the release work on Quake Live in Windows, download the embedded version of Python 3.11 for 32 bits, available [here](https://www.python.org/ftp/python/3.11.1/python-3.11.1-embed-win32.zip).
- Download the latest [release](https://www.github.com/locopol/WinQLX32/releases/latest) and extract the contents in your Quake Live base folder (installed via Steam Client or SteamCMD for Windows).
- Download the latest MinoMino minqlx plugins available [here](https://github.com/MinoMino/minqlx-plugins/releases/tag/v0.3.7) and extract the folder in Quake Live base folder, rename the extracted folder to `minqlx-plugins`
- Copy the contents of Python embedded zip in folder `[Quake Live directory]\deps\python_embed`.

- execute `run_server_x86_WinQLX32.bat` script and wait the qconsole initialization, if you have a trouble, run as `administrator`.

**NOTE** : The code in some runs enter in a **race condition** and can't hook the necesary functions to work (the log console must show the `late.init` minqlx initialization), this can be resolved closing the Quake Live console (pressing `Quit` button) and launch the script again.

Configuration
=============

**WinQLX32** like minqlx is configured using cvars, like you would configure the server. All minqlx cvars should be prefixed with qlx_. The following cvars are the core cvars. For plugin configuration see the plugins repository.

    qlx_owner: The SteamID64 of the server owner. This is should be set, otherwise minqlx can't tell who the owner is and will refuse to execute admin commands.
    qlx_plugins: A comma-separated list of plugins that should be loaded at launch.
        Default: plugin_manager, essentials, motd, permission, ban, silence, clan, names, log, workshop.
    qlx_pluginsPath: The path (either relative or absolute) to the directory with the plugins.
        Default: minqlx-plugins
    qlx_database: The default database to use. You should not change this unless you know what you're doing. (deprecated)
        Default: Redis 
    qlx_commandPrefix: The prefix used before command names in order to execute them.
        Default: !
    qlx_redisAddress: The address to the Redis database. Can be a path if qlx_redisUnixSocket is "1". (deprecated)
        Default: 127.0.0.1
    qlx_redisDatabase: The Redis database number. (deprecated)
        Default: 0
    qlx_redisUnixSocket: A boolean that determines whether or not qlx_redisAddress is a path to a UNIX socket. (deprecated)
        Default: 0
    qlx_redisPassword: The password to the Redis server, if any. (deprecated)
        Default: None
    qlx_logs: The maximum number of logs the server keeps. 0 means no limit.
        Default: 5
    qlx_logsSize: The maximum size in bytes of a log before it backs it up and starts on a fresh file. 0 means no limit.
        Default: 5000000 (5 MB)


Usage
=====

Once you've configured the above cvars and launched the server, you will quickly recognize if for instance your database configuration is wrong, as it will start printing a bunch of errors in the server console when someone connects. If you only see stuff like the following, then you know it's working like it should:

```
[minqlx.late_init] INFO: Loading preset plugins...
[minqlx.load_plugin] INFO: Loading plugin 'plugin_manager'...
[minqlx.load_plugin] INFO: Loading plugin 'essentials'...
[minqlx.load_plugin] INFO: Loading plugin 'motd'...
[minqlx.load_plugin] INFO: Loading plugin 'permission'...
[minqlx.late_init] INFO: We're good to go!
```

To confirm minqlx recognizes you as the owner, try connecting to the server and type !myperm in chat. If it tells you that you have permission level 0, the qlx_owner cvar has not been set properly. Otherwise you should be good to go. As the owner, you are allowed to type commands directly into the console instead of having to use chat. You can now go ahead and add other admins 
to with !setperm. To use commands such as !kick you need to use client IDs. Look them up with !id first. You can also use full SteamID64s for commands like !ban where the target player might 
not currently be connected.

See [here](https://github.com/MinoMino/minqlx/wiki/Command-List) for a full command list.

**Note** that the plugins repository only contains plugins maintained by `MinoMino` and `mgaertne`. Take a look here some of the plugins by other users that could be useful to you, **i can't guarantee the correct functionality of these plugins because the majority has been created more than 7 or 10 years ago, so, you are advised**.

Updating
========

Since this and plugins use different repositories, they will also be updated separately. However, the latest master branch of both repositories should always be compatible. If you want to try 
out the develop branch, make sure you use the develop branch of both repositories too, otherwise you might run into issues.

To update the core, download the latest release and copy the contents to `Quake Live Directory` or clone this repository, compile `WinQLX32` and copy `WinQLX32\bin` contents to `Quake Live Directory`, just like you did it, when following installing instructions. To update the plugins, use cd to change the working directory to `[Quake Live Directory]\minqlx-plugins` and do git pull origin and you should be good to go. Git should not remove any untracked files, so you can have your own custom plugins there and still keep your local copy of the repo up to date.

You can also try running these scripts from your `Quake Live Directory`. It will compile the latest version from source and update plugins. The second script is the same, but using the develop branch instead.

Compiling from source
=====================

If you want modify some parts of core for testing or debugging, the source code can be compiled using Microsoft Visual C (I'm using MSVC 19.43.34808.0).
+ Follow this steps:
    - Install Cmake for Windows 4.4.0 or later to compile sources. 
    - The source need an installed version of Python 3.11 for 32 bits to be compiled (**the embedded version not work because doesn't have the required libraries and header files**), the installer is available [here](https://www.python.org/ftp/python/3.12.1/python-3.11.1.exe).
    - the source code include a **Minhook** precompiled lib in `deps/lib/MinHook.lib` (used in `CmakeFile` for fast compilation) and the source code in folder `deps/minhook`, if you want to add the source code of minhook in the compilation, configure the C and H files into `CmakeFile` rules to compile with the `WinQLX32` source code directly.
    - Open a cmd console and run the **Native Tools Command Prompt for x86**, available in your Visual Studio Installation, e.g: "C:\Program Files\Microsoft Visual Studio\\[`VERSION`]\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
    - Clone the repo with `git clone https://github.com/locopol/WinQLX32.git`
    - The build can be tested in 2 scenarios: Python included (minqlx core) or No Python (WinQLX32 base hooks for testing):
        + Without Python:
            - `cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -B build`
        + With Python:
            - `cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -B build -DPY_PATH=<your Python path installation>`
    - compile with: `cmake --build build --config Release`

    - After succesful compilation, the dll file (winqlx.dll) and zip file (minqlx.zip) are copied to `bin\` directory, put these files into Quake Live installation directory and test. *(remember put the embed python in deps\python_embed directory and minqlx-plugins in base Quake Live directory)*.

Contribute
==========

If you'd like to contribute with code, you can fork this or and create pull requests for changes. Please create pull requests into the develop branch and not to master.

If you found a bug, please open an issue here on Github and include the relevant part from either the server's console output or from minqlx.log which is in your fs_homepath, preferably the latter as it is more verbose. Note that minqlx.log by default becomes minqlx.log.1 whenever it goes above 5 MB, and keeps doing that until it goes to minqlx.log.5, at which point the 5th one gets deleted if the current one goes over the limit again. In other words, your logs will keep the last 30 MB of data, but won't exceed that.

Both when compiling and when using binaries, the core module is in a zip file. If you want to modify the code, simply unzip the contents of it in the same directory and then delete the zip file. minqlx will continue to function in the same manner, but using the code that is now in the minqlx directory.

ToDO List
=========

This development requires optimizing or fix several areas to pass the beta phase; this to-do list shows the current WIP, feel free to contribute to improve or correct it.:

| Area | Status | Notes |
|------|--------|-------|
| bg_itemlist pointer | Pending | Need more investigation to pinpoint the correct pointer of bg_itemlist required for holdable functions.
| Missing Hooks | Pending | Need more investigation to pinpoint more functions of quake live binary and qagamex86.dll library.
| Workshop integration | Pending | Need more investigation to made the full functionality of workshop elements in server and transfer references to clients, the pak00.pk3 contents is working in beta stage.
| Q3console visibility | Pending | Need a method to inject a code to hide the q3console in windows and put all logging information directly to file or put all logs of WinQLX32 into Q3console if visibility can't be changed.
| 3rd Party Plugins | Pending | Require more exaustive tests of third party plugins to validate core code and made full compatibility.
| ZMQ in Python embed | Pending | Add full compatibility with libZMQ to enable minqlx ZMQ procedures for full compatibility.
| Launcher refactor | Pending | The rudimentary launcher is used to hook a unique quakelive_steam.exe process with winqlx.dll, so, the launcher can't work with multiple instances in the same server.
| Custom Python Path | Pending | Cmakefile only compile the sources to get Python files from relative path of Quake Live binary (deps\python_embed), the Launcher need a refactor to implement environment variables or parameters in Cmakefile to find dinamically the Embedded or full installation of Python.
| PatternSearch routines | Pending | The current beta run the hooks using direct calls to pointers for working, the main code need reutilize the pattern routines to optimize search functions.

Thanks
======

- **MinoMino**: by the source code of minqlx, all parts of original code has been reutilized to made WinQLX32, thanks for the **PRO** Tip ;-).
- **tjone270**: To mantain alive minqlxtended and the ioquakelive repository which helped me to build this code.
- **RizinOrg and the Cutter software for reverse engineering, is a excellent tool for this Port**.
- **Google Gemini**: for learning path, translation of routines, code compression and assembly interpretation.