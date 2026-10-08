# User game-data staging

The PSP port uses data from the user's legally obtained Dead Space Mobile copy.
The repository contains only the staging tool and metadata; commercial game
assets and binaries are not committed to the public repository.

## Stage the APK

```bash
python3 tools/stage_deadspace_apk.py path/to/com.ea.deadspace*.apk -o psp_data
```

The resulting tree is suitable for the runtime's expected `ms0:/data/deadspace/`
layout:

- `published/` — game maps, models, textures/resources, fonts and scripts
- `EAMCore.ini`, `EAMToolManager.ini` — engine configuration
- `guest_libs/libDeadSpace.so` — original ARM guest library for local use

Do not commit `psp_data/` to a public repository. The build system will use a
locally supplied data directory when the runtime integration reaches the game
boot stage.
