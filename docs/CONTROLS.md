# PSP control layout

User-requested gameplay layout: analog stick moves the player; face buttons move
the camera. This mapping is implemented in the input translator and ARM JNI
argument packer. **It is not yet usable gameplay:** the diagnostic EBOOT does not
run the game loop, and the original game currently exits input callbacks early
until its first-frame initialization completes.

| PSP input | Gameplay action |
| --- | --- |
| Analog stick | Player movement |
| Triangle / Cross | Camera up / down |
| Square / Circle | Camera left / right |
| L / R | Aim / fire |
| D-pad Up | Interact with doors/items |
| D-pad Down | Reload |
| D-pad Left / Right | Previous / next weapon |
| Start | Open pause/menu |
| Select (tap and release) | Show HUD |
| Select + D-pad Up | Quick turn; stasis while aiming |
| Select + D-pad Down | Kinesis |
| Select + D-pad Left / Right | Plasma saw attack / locator |

Select acts as a modifier while held; using a combination suppresses its HUD tap.
Changing modifiers while an action remains held releases the previous binding
before pressing its replacement. Focus loss or unavailable PSP input releases
held keys/touches, preventing stuck movement/aim/fire. Opposite camera directions
cancel. Stick center has a 24-unit deadzone and reaches full scale at both ends.

Poll once per display frame. Camera input sends a touchpad down/move/up gesture
each frame, so a held face button continues to request movement. Movement keeps a
separate touch pointer down until the stick returns to center. Coordinates scale
with the guest display size; the host probe currently uses 480x272. Sensitivity,
axis direction and touch anchors still require real gameplay verification.

## Remaining input work before Beta 1

- Menu navigation needs a visible virtual touch cursor: the original menus do not
  provide complete gamepad navigation. Start is mapped, but menu selection is not
  yet solved on a PSP without a touchscreen.
- Weapon rotation and zero-gravity jump require accelerometer gesture emulation.
  They are not represented by fabricated key presses or claimed functional yet.
- Connect physical PSP polling and event dispatch to the actual initialized game
  loop, then test simultaneous movement/camera/aim/fire and every context action.
- Tune deadzone/sensitivity and confirm mappings against this exact donor during
  gameplay. No controls-complete or playable-beta claim follows from unit tests.

## Evidence and interfaces

The exact APK DEX declares `NativeOnPointerEvent(int,int,int,float,float)` and the
ARM binary consumes its pointer ID and float words from the stack after r0-r3.
Native pointer event IDs were verified from its getter instructions:
down 0x6000c, move 0x4000c, up 0x8000c. Keyboard callbacks use three integer
arguments after env/object. Touchscreen, touchpad and keyboard module IDs are
1000, 1100 and 600 respectively. These are guest IDs, not host pointers.

The active [Dead Space Vita reference](https://github.com/v-atamanenko/deadspace-vita/tree/3eaa3c4b8a0201eec746f7676e0bddcffc5d5638)
provided action-label and touch-profile context. The PSP translator is new code;
Vita gameplay is not evidence of PSP performance or compatibility. Its touch
profile is a starting point, not a verified PSP calibration.
