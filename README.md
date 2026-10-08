# Dead Space — PSP Port

PSP homebrew porting workspace for Dead Space Mobile (Xperia Play).

The Vita project is a reference, not a directly reusable executable: the donor game is ARMv6 while PSP is MIPS32.

## Goal
Build a native PSP EBOOT.PBP and progressively implement the runtime needed by the user's legally obtained Xperia Play game data.

## Status
- PSP bootstrap: started
- Native PSP entry point: added
- GU initialization: added
- Android/Bionic/JNI ABI: next
- ARMv6 -> MIPS execution strategy: central investigation
- Playable game EBOOT: not yet complete

No EA game binary or copyrighted assets are included.

Expected donor library: lib/armeabi/libEAMGameDeadSpace.so
Expected SHA-1: 0ed42b611415015807f759ec9b5457857143ce39

## Strategy
1. PSP runtime
2. Android/Bionic/JNI ABI
3. CPU execution strategy
4. PSP GU renderer
5. audio/input/filesystem
6. game boot
7. PPSSPP regression testing
