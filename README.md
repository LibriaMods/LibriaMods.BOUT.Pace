# LibriaMods BOUT Pace

Faster game clock for **The Book of Unwritten Tales** (2009, 32-bit, OGRE engine).

The game has no scripting hook for speed, so this is a tiny `dinput8.dll` proxy: the game's input library
(OIS) loads it from the game folder, it forwards `DirectInput8Create` to the real Windows DLL and scales
`QueryPerformanceCounter` / `GetTickCount` in the game's modules.

| Key | Action |
|---|---|
| **F5** | Speed on/off (default ×2) |
| **F6** / **F7** | Slower / faster (0.5 steps, 1–6×) |

Settings: `LibriaPace.ini` next to `bout.exe` (created on first start). In windowed mode the title shows the speed.
Everything speeds up while it's on (walking, animations, timers); voices keep their pitch.

## Install
Copy `dinput8.dll` next to `bout.exe`. Delete it to uninstall.

## Build
`i686-w64-mingw32-gcc -O2 -shared -s -o dinput8.dll src/pace.c src/dinput8.def -Wl,--kill-at -static-libgcc`
