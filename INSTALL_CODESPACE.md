# One-command Codespace bundle build

Open or rebuild the Codespace so .devcontainer/devcontainer.json is used.

Upload your legally owned Dead Space Mobile APK into the Codespace, then run:

    bash tools/codespace_build_bundle.sh /workspaces/Ds-psp/your-game.apk

If the APK is somewhere under /workspaces, the argument can be omitted:

    bash tools/codespace_build_bundle.sh

The command builds the current PSP EBOOT and creates:

    DeadSpace-PSP-Ready.zip

The ZIP contains:

- PSP/GAME/DEADSPACE/EBOOT.PBP
- data/deadspace/ — complete APK assets/ tree
- android_apk/ — complete APK contents for local debugging
- BUNDLE_MANIFEST.json
- INSTALL.txt

Copy PSP/ and data/ to the PPSSPP memory-stick root.

Commercial game data is generated locally from the user's own APK and is not
committed to GitHub.
