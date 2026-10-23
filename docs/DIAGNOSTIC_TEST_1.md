# Unofficial PSP Diagnostic Test 1 — NOT PLAYABLE

This is an early runtime test, **not Beta 1 of the game**. There is no game menu,
rendering, audio, input-to-game mapping, level loading or gameplay. This project
is unofficial and is not endorsed by EA. The package contains no original game
library, assets or saved games. No PSP model or PPSSPP compatibility is yet verified.

## Install and run

1. Extract the ZIP so `PSP/GAME/DEADSPACE/EBOOT.PBP` is on your PSP memory stick.
   For PPSSPP, place that folder inside its configured memory-stick folder (often
   the user-selected PSP folder), or browse to EBOOT.PBP in PPSSPP's file browser.
2. Launch **Dead Space Diagnostic Test 1**. The expected output is a text screen
   marked **NOT PLAYABLE**, an ARM self-test result and optional library status.
3. Without a game library, `Donor load: -2` is expected. No game data is needed to
   test the screen, CPU self-test and exit control.
4. Press **START** to exit. PSP HOME exit is also registered.
5. Send a screenshot, your PSP model/firmware or PPSSPP version/settings, and
   `PSP/GAME/DEADSPACE/psp_port.log` if it was created. Report a black screen,
   launch error or failure to exit; none of those mean the game has initialized.

If you already have the previously staged donor library, the optional path is
`ms0:/data/deadspace/guest_libs/libDeadSpace.so`. It is read, loaded, linked and
stepped for **one instruction only**. Constructors and JNI are not run by this
package. Do not rename another game's executable or library into that path.

The diagnostic writes/replaces its own `psp_port.log` on launch. It does not edit
game assets or saves. Back up an earlier diagnostic log if you want to keep it.
Do not overwrite an existing unrelated `DEADSPACE` folder when extracting.

## What has been checked

PSP cross-compilation and package structure are checked in the task. CPU/runtime
regressions run on the host under sanitizers; VFP transfer vectors were compared
with an independent ARM emulator. **This package has not been boot-tested on a
physical PSP or in PPSSPP.** A successful compilation is not device validation.

The next playable-release gates include all constructors and JNI startup, real
game graphics/audio/controls, a playable scene and measured device memory/speed.
No reliable completion date can be given yet.
