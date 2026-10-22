# Dead Space Mobile to PSP: implementation plan

## Objective and completion rule

Run the original game's code and data on PSP, with functioning graphics, input,
audio, saving/loading and measured performance. A blank screen, EBOOT that only
boots, Android wrapper, or successful compilation does not meet this objective.
No milestone is complete on the strength of a plan or a stubbed success return.

The route is to execute the original ARM guest behind a PSP compatibility layer.
The existing interpreter is an initial correctness tool, not a proven solution
for playable speed. We will evaluate translation/optimization before claiming
performance. The Vita reference is useful for Android interfaces, but Vita ARM
code cannot execute directly on the PSP's MIPS CPU.

## Donor baseline

The inspected 1.2.0 APK has SHA-256
`00c0a997489f81ab18c2c5e6849e7f2f516e527f8b4c4e13e502f130f8a86bfe`.
Its `assets/guest_libs/libDeadSpace.so` has SHA-256
`69047bddefa0d23e78e9f3299043c65476a1d771a9bdce6fd2a18b4793b7fa50`.
The ARM32 ELF reports v5TE, Thumb-1 and VFPv2 attributes; the separate ARM64
Android wrapper contains Dynarmic markers and is not the PSP executable.

This guest differs from the Xperia Play donor previously listed in README.md.
Support for one binary must not silently imply support for the other. Record
hashes in every trace and isolate any donor-specific assumptions. Keep original
binaries/assets and personal saves out of commits.

## Milestones and gates

| Stage | Work | Acceptance gate | Current state |
| --- | --- | --- | --- |
| M0: reproducible foundation | Restore existing source; reproduce EBOOT build; add CPU regression tests and bounded guest trace | PSP build/package passes; supported instruction regression groups pass under sanitizers; donor identity recorded | Build and 13 CPU groups pass locally; physical/emulator boot not yet verified |
| M1: loader and linking | Validate ELF/program tables; guest address model; complete supported relocations and stable import ownership; initialize arrays/stack | Synthetic relocation/error fixtures pass; all 208 donor init-array entries are accounted for; unresolved targets stop with symbol/PC; no ELF header/data executed after a null branch | Next: current unrelocated first-constructor trace reaches an unresolved PLT target |
| M2: CPU execution | Correct ARM/Thumb instruction decoding, flags, memory alignment and faults; implement required VFP or verified software equivalent | Deterministic instruction vectors and cross-checks against an independent ARM implementation; exact guest trace advances with bounded memory; unsupported encodings stop explicitly | Partial interpreter only; VFP and many instructions remain absent |
| M3: Android/JNI startup | Build exact JNI table/lifecycle, native registrations, libc/Bionic, threads, timers, asset and file adapters | Original JNI_OnLoad/registration and app lifecycle complete without required stubbed calls; repeatable init/shutdown; heap/stack high-water logs | Unimplemented beyond a few memory/string imports |
| M4: first genuine frame | Map exercised GLES 1.x functions to GU/GUM; handle state, geometry, textures, formats and render targets | Original game produces a recognizable menu/loading frame; captures and logs confirm real guest rendering; no fabricated image | Not reached |
| M5: interactive menu | PSP analog/buttons mapped to original events; lifecycle/focus, audio decoding/output and timing | Menu navigation, audible output, pause/resume and input work repeatedly; teardown is clean | Not reached |
| M6: playable slice | Start a level, movement/aim/shoot, enemy/game logic, HUD, saving and loading | A documented gameplay scenario and save/reload pass; no required unimplemented import on the tested path | Not reached |
| M7: memory and speed | Measure CPU/GPU/audio time and memory by scene; optimize hotspots, caching/translation and asset streaming | Recorded frame times, peak memory and audio underruns on declared PSP models; agreed playability target met without corrupting behavior | Performance unknown; no success forecast |
| M8: release checks | Test multiple scenes, transitions, death/restart, languages, save errors, missing/corrupt data; package reproducibly | Device/PPSSPP evidence, artifact hashes, setup instructions, compatibility matrix and explicit limitations accompany the original-game build | Not reached |

M1 and M2 will iterate together: linker and instruction gaps must be distinguished
from one another. M3 needs both. Graphics can be developed using independent
fixtures in parallel with CPU work, but reaching M4 requires the original game.

## Validation procedure

One local command, with a prepared PSPDEV toolchain on PATH:

```sh
bash tools/validate.sh
```

This runs the host CPU regression groups under AddressSanitizer/UBSan, then
builds the PSP EBOOT. It does not install an emulator or download game data.
Host-only checks are also available independently:

```sh
cmake -S tests -B build-host -DDSPSP_SANITIZE=ON
cmake --build build-host -j2
ctest --test-dir build-host --output-on-failure
```

Optional probe of the user's local guest library:

```sh
build-host/probe_guest /path/to/libDeadSpace.so
build-host/probe_guest /path/to/libDeadSpace.so 0x27e72c 512
```

These are **unrelocated**, bounded entry probes. They do not implement Android,
call host native imports or initialize the game. Exit 0 means the selected
routine returned to the sentinel; exit 2 means execution stopped or reached its
instruction limit; exit 1 means loader failure. An entry probe returning is not
proof that constructors or JNI startup work.

## Handling risks without hiding them

- Stop at unknown instructions, invalid targets and unimplemented required
  services. The legacy import bridge currently returns zero for many unknown
  services; it must be replaced with explicit capability/error handling before
  full game initialization. Never count such a return as successful integration.
- Prove relocation semantics, import ownership and guest-memory bounds before
  expanding execution. The current linker/import bridge needs dedicated tests.
- Measure both 32 MiB and 64 MiB PSP memory budgets. No model support is claimed
  until its budget and runtime checks pass. APK/storage size is not peak RAM.
- Prototype CPU performance early with real instruction traces. If correct
  execution is too slow, assess an ARM-to-MIPS translation strategy with tests;
  an interpreter throughput estimate is not a gameplay benchmark.
- Asset conversion must preserve rendering/audio behavior. Work on copies, keep
  fixtures, and compare visual/audio output; do not discard data to make it fit.
- Every failure needs its donor hash, build revision, PC/opcode or import name,
  reproduction command and next experiment. External or hardware limits remain
  visible until resolved. Planning cannot guarantee that none will occur.

## Evidence required before calling it playable

The release record must contain installation/cold-launch checks, actual menu and
level captures, controls/audio checks, save/reload checks, frame-time and memory
measurements, crash/error logs and the tested PSP model/firmware or PPSSPP build.
No original-game executable is currently claimed to pass these gates.
