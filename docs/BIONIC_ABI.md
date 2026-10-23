# Legacy Android guest data ABI

The port uses guest addresses and explicit little-endian bytes. Host libc tables
and host pointer sizes are not substitutes for the Android ARM ABI.

## Character tables

Reference: AOSP Bionic tag `android-4.0.1_r1`:

- [ctype.h](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/include/ctype.h)
- [ctype_.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdlib/ctype_.c)
- [tolower_.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdlib/tolower_.c)
- [toupper_.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdlib/toupper_.c)

`_ctype_`, `_tolower_tab_`, and `_toupper_tab_` are pointer variables, each four
bytes in the ARM guest. Each points to 257 entries: EOF followed by bytes 0–255.
Classification entries are bytes; conversion entries are signed 16-bit values.
EOF is 0 for classification and -1 for conversion. Conversion preserves bytes
128–255. Classification marks 128–159 as controls and 160–255 as punctuation.
The guest's inline functions index `(table + 1)[c]`.

The runtime constructs these tables without depending on host locale. The tests
compare all 771 entries against table fixtures extracted from the reference
sources, with their BSD license notices retained. Data resolution allocates each
object once per initialized image and returns the address of the pointer variable.
Call `ds_imports_init()` before binding a freshly loaded image, including when
reusing a `DsArmImage` structure. This bridge is single-image and single-threaded.

This reference establishes the compatibility implementation's baseline, not the
exact Android version the donor was compiled against. Real-donor execution is
still required to verify its use of this ABI.

## Standard streams and runtime globals

`__sF` now provides three 84-byte ARM FILE structures and three 12-byte
extensions, initialized from the historical
[stdio.h](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/include/stdio.h),
[findfp.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdio/findfp.c),
[fileext.h](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdio/fileext.h),
and [wcio.h](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdio/wcio.h).
Offsets: flags 12, descriptor 14, cookie 28, callbacks 32–44, extension 48.
Callbacks use registered guest trampolines. Input, seek, close, wide I/O and
arbitrary file streams remain unsupported and stop explicitly.

`fwrite`, `fputs` and `__swrite` send stdout/stderr bytes directly to the port log.
No text formatting, prefix or newline is added to these bytes. Short writes and
sink failure set the stream error flag. `fileno`, `ferror`, `clearerr` and
`fflush` support these standard stream objects; `fflush` has no buffered output
to drain. `fflush(NULL)`, errno propagation and guest file opening still need
implementation. Supporting console output does not establish asset/save I/O.
The log formatter also caps truncated messages before writing, avoiding reading
past its 512-byte buffer when vsnprintf reports a longer required length.

`__page_size` is a four-byte value 4096, matching
[ARM page.h](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/kernel/arch-arm/asm/page.h)
and [libc_init_common.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/bionic/libc_init_common.c).
`tzname` contains two guest pointers to the initial three-space `WILDABBR` from
[localtime.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/tzcode/localtime.c).
This is the pre-tzset state; timezone conversion remains unsupported.

`__stack_chk_guard` uses the four-byte terminator fallback `00 00 0a ff` from
[ssp.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/bionic/ssp.c).
The port does not yet provide randomized canaries. `__stack_chk_fail` logs the
fault and stops guest execution with error -33; the CPU reports import failure
as -30. It never returns success to corrupted guest code.

## Mutexes during single-thread initialization

The bridge implements mutex attributes, initialization, destruction, lock,
trylock and unlock using the 32-bit guest layout from historical
[pthread.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/bionic/pthread.c).
Types normal/recursive/error-check are 0/1/2 in attributes and bits 14–15 in
mutexes. Bits 2–12 hold recursive depth beyond the first lock; bits 16–31 hold
the owner for recursive/error-check mutexes. The single guest thread has ID 1.
Static initializers work without a host-side registry. Android error values are
returned for invalid arguments, busy trylocks and ownership errors.

This is not a threading implementation. Thread creation, timed locking, shared
mutexes, contended states and any lock needing a wait stop execution explicitly.
Before adding a scheduler, replace the fixed owner ID and serialize state
transitions; the current bridge must not run concurrently. As conservative
limits, destroying a held recursive mutex returns EBUSY and recursive count
exhaustion returns EAGAIN. The old Bionic source instead allowed trylock-based
destruction by the owner and wrapped its recursive counter; those behaviors are
not reproduced. Bad guest memory or alignment stops before state mutation.

## Execution checkpoint

All seven required data globals resolve for the recorded donor. The bounded
first-constructor probe returns to its sentinel after 1,640 successful steps,
including cleanup registration. Its two mappings (16,781,312 and 16,777,216 bytes)
still fail ENOMEM. Returning from this routine does not establish a usable heap,
successful game initialization or correctness of all interpreter instructions.
The persistent host sequence now returns from all 208 constructors, with none
skipped. JNI startup remains untested. An explicit host
experiment with an 18 MiB mapping pool now services the large allocation and
returns after 1,584 steps; see the budget measurements below.

ELF PLT stubs that load PC directly dispatch registered imports before instruction
fetch; synthetic tests cover this path independently of the donor.

## Program break and arena ownership

The loader rounds the mapped ELF span up to a 4096-byte boundary, then appends
the same 8 MiB arena as before. It now partitions that arena into 1 MiB for bridge
objects/imported malloc, 4 MiB for the guest program break, 2 MiB for anonymous
mappings, and 1 MiB for the stack. These are initial diagnostic budgets, not verified gameplay budgets.
`heap_size` still describes the entire mapped arena; `bridge_size` limits the
bridge bump allocator. Main and the host probe use the explicit stack range.
Synthetic images without a partition retain their previous heap-only behavior.

`sbrk` interprets its argument as signed 32-bit, returns the old break, supports
zero-increment queries and shrinking, and rejects crossing either pool boundary
with `(void*)-1` and Android ENOMEM (12). `__errno` returns a stable guest address
for the single execution thread; success preserves an existing errno value.
Changing the break neither allocates bridge objects nor clears reclaimed bytes.
Future thread support must replace the single errno slot with thread-local state.

Pool separation prevents allocator overlap; it does not provide memory access
permissions or a stack guard page. The interpreter still sees a contiguous arena.
Bridge free/realloc and general memory mapping remain incomplete.

## Anonymous mappings

`mmap` supports ARM32 arguments (the fd and offset are on the guest stack),
PROT_READ|PROT_WRITE (3), MAP_PRIVATE|MAP_ANONYMOUS (0x22), fd=-1 and offset=0.
Non-fixed address hints may be ignored. A bounded ownership bitmap (up to 32 MiB for explicit experiments) implements
first-fit contiguous allocation inside the dedicated 2 MiB pool. New mappings
are zero-filled, including reused pages. `munmap` rounds lengths to pages,
supports partial release and unmapped holes within this pool, and rejects ranges
outside the pool rather than touching bridge, program-break, stack or ELF bytes.
Zero lengths and unaligned unmap addresses return EINVAL; exhausted/oversized
mapping requests return MAP_FAILED with ENOMEM, without changing ownership.

Fixed mappings, file-backed/shared mappings, other protections, mmap64, mremap
and mprotect stop as unsupported. Unmapping releases allocation ownership but
does not yet enforce access faults in the flat interpreter memory model.
The prior 6 MiB program-break pool is reduced to 4 MiB to retain the total 8 MiB
arena; this is a diagnostic allocation budget change, not a PSP model guarantee.
The next work includes larger-budget measurements, C++ cleanup registration and
constructor enumeration. Successful donor mmap is now recorded in the explicit
18/32 MiB host experiments below; the default budget still exhausts.

## C++ cleanup registration

`__aeabi_atexit(object, destructor, dso)` and
`__cxa_atexit(destructor, object, dso)` now retain guest callback addresses,
arguments and DSO identities in a bounded 1,024-entry registry. A full registry
or null callback returns -1; success returns zero. No host callback is invoked.
The future shutdown runner can use `ds_import_take_destructor` to remove the
newest matching entry before executing it, preserving reverse registration
order and preventing duplicate invocation. DSO zero selects all registrations.
Registry state resets with import initialization or a different image binding.

This implements registration only. `__cxa_finalize`, exit processing and actual
destructor execution still stop as unsupported; no clean shutdown is claimed.
Synthetic tests cover both argument orders, Thumb callback addresses, DSO
filtering, reverse ordering, exhaustion, reuse and reset. The reference contract
is [Bionic atexit.c](https://android.googlesource.com/platform/bionic/+/android-4.0.1_r1/libc/stdlib/atexit.c)
and the ARM C++ ABI's __aeabi_atexit argument order.

## Measured memory budgets (first constructor only)

`ds_arm_loader_load_with_mmap` accepts a page-aligned, nonzero mapping budget up
to 32 MiB. The ordinary loader and PSP main still use the original 2 MiB mapping
pool. Host probe syntax adds an optional final mapping budget in MiB:

```sh
build-host/probe_guest /path/to/libDeadSpace.so 0x27e72c 100000 18
```

| Mapping pool | Total guest image + arena | Peak live mappings | Calls / ENOMEM failures | Steps to return |
| --- | ---: | ---: | ---: | ---: |
| 2 MiB (default) | 13,508,608 bytes | 0 | 2 / 2 | 1,640 |
| 18 MiB (host experiment) | 30,285,824 bytes | 16,781,312 bytes | 1 / 0 | 1,584 |
| 32 MiB (host experiment) | 44,965,888 bytes | 16,781,312 bytes | 1 / 0 | 1,584 |

All three traces use the unchanged recorded donor. Bridge allocation use is
1,616 bytes and program-break use is zero at this checkpoint. Peak mappings count
page-rounded live allocations, not bytes actually touched. Total guest bytes are
allocated and zeroed by the loader; they exclude host/PSP runtime overhead,
renderer buffers, future assets, audio and thread stacks. These results are not
RSS measurements, performance measurements or a PSP model compatibility claim.
The 18 MiB experiment removes an artificial allocation failure in the first
constructor; it does not validate all initialization or gameplay.

Telemetry reports mapping call count, errno-return failures, largest request,
current live mapping bytes and peak mapping bytes. Unsupported operations stop
execution and are not counted as errno-return failures. Partial unmapping and
repeated unmapping of holes do not underflow live-byte accounting. Counters reset
on initialization or binding a different image.

The persistent sequence now completes all 208 constructors without further mapping
requests. Next: measure JNI/lifecycle startup before reducing reservations or
sharing unused capacity. Do not infer that unused space at this one
checkpoint stays unused during gameplay. The supplied GTA PBP's MEMSIZE=1 and
pool/streaming strings are reference clues only; no matching source was verified,
so no allocator technique is attributed to that executable.

## Persistent initializer probe

```sh
build-host/probe_guest /path/to/libDeadSpace.so constructors 100000 18
```

The loader reads DT_INIT and DT_INIT_ARRAY metadata, validates size/alignment
and image-span bounds, and exposes the array address/count. The probe reads array
entries **after relocation**, calls DT_INIT first when present, and visits array
entries in order. Zero and UINT32_MAX entries are explicitly counted as skipped.
Memory, allocator ownership, errno and destructor registrations persist between
calls; each routine receives a fresh CPU context and reserved stack top. The
instruction budget is per routine. Any fault, invalid target or budget exhaustion
stops the sequence; later entries are never treated as tested. An exact-budget
return to the sentinel is recognized. This diagnostic runner does not perform JNI
startup, dependency constructors, full process argument setup or shutdown.

Independent ELF fixtures verify init-before-array order and persistent memory:
each routine checks a counter written by its predecessor before updating it.
Other fixtures test sentinel entries, invalid metadata/targets, failure stopping
and the exact instruction-limit boundary. Python 3 is required for this host test.

Exact donor result with an 18 MiB mapping pool after startup instruction/import support:
208 total entries, 208 attempted, 208 returned, zero skipped, zero pending;
8,264 successful steps. Live/peak mapped bytes remain 16,781,312, bridge use 1,616,
program-break use zero, total mapped image+arena 30,285,824. No allocation failures
occurred. This proves bounded host constructor completion, not full game startup.

## VFPv2 register and memory transfers

The CPU now stores raw S0–S31 words; D0–D15 alias adjacent words (low word first).
VLDR/VSTR and VLDM/VSTM support single/double transfers, PC-relative literals,
increment-after and decrement-before addressing, including VPUSH/VPOP. Complete
ranges, register counts, alignment and writeback addresses are checked before
mutating memory/registers. Condition-failed instructions remain no-ops. Invalid
lists, D16–D31 and unsupported arithmetic still stop explicitly. Required scalar
arithmetic and FPSCR semantics are described below; VFP coverage remains partial.

The host regression group tests bit-exact transfers (including NaN payloads and
signed zero), aliasing, stack restoration, writeback, conditions and fault atomicity.
`tools/check_vfp_oracle.py` additionally compares 266 deterministic valid vectors
against Unicorn 2.1.4, checking all VFP registers, core registers, PC and memory.
It is optional (requires Unicorn and a host C compiler); normal CI tests require
neither Unicorn nor donor files. Capstone 5.0.7 independently identified the donor
VPUSH and current STRH boundary. No host float approximation was introduced.


## ARM halfword and signed transfers

STRH/LDRH/LDRSB/LDRSH support immediate and register offsets, adding/subtracting,
pre/post indexing, writeback and PC-relative loads. Signed loads extend to 32 bits;
stores preserve adjacent bytes. LDRD/STRD support even register pairs through
R12/R13 and 8-byte-aligned accesses. Unaligned halfwords/doublewords, invalid
operands and unprivileged variants stop explicitly. Address arithmetic wraps at
32 bits, then the actual access is checked against the mapped image. Faults do not
change memory, destination/base registers, PC or step count (the existing fetch
path still refreshes the pipelined R15 value). Synthetic sanitizer tests and 360
Unicorn comparisons cover supported addressing modes. No new gameplay claim.


## Constructor CPU support

- Fixed the arithmetic decoder so immediate constant bits cannot be mistaken for
  register-mode encodings. Added CLZ and signed/unsigned long multiply/accumulate.
- VMOV core/single and core-pair/double transfers preserve raw bits. VCMP/VCMPE
  binary32 comparisons handle NaNs, signed zero and FPSCR NZCV/invalid flags;
  VMRS transfers FPSCR or its NZCV bits to the ARM condition flags.
- A pinned Berkeley SoftFloat 3e subset with ARM-VFPv2 specialization implements
  binary32 add/subtract/multiply/divide and non-fused multiply-accumulate/subtract,
  binary32/binary64 conversion and signed/unsigned 32-bit integer-to-float conversion.
  Rounding modes and cumulative exception flags are preserved. Default NaN mode
  is supported; FZ, vector modes, enabled exception traps, double arithmetic and
  other unimplemented opcodes stop explicitly. SoftFloat global state assumes the
  existing single guest thread. Its BSD license accompanies CI binary artifacts.
- 32,720 deterministic reference cases match Unicorn 2.1.4 registers, FPSCR,
  condition flags and memory. Separate transfer checks cover 266 VFP and 360
  integer memory vectors. This does not establish complete ARM ISA coverage.

## Startup imports

- `clock`: Bionic signed 32-bit microseconds, with unavailable/unrepresentable
  values returned as -1. Host probes use C process time. PSP newlib's clock is
  system uptime (verified from its linked `_times` implementation), so PSP uses
  the single guest execution thread's kernel run time instead. The adapter has
  mocked kernel unit tests and a PSP compile check; device timing remains untested.
- `time`: signed 32-bit seconds and optional bounded guest output pointer. Times
  outside ARM's range stop instead of silently truncating. Host failures return -1;
  mapping host errno remains unimplemented.
- ARM C++ guards: four bytes, initialized bit 0, private pending bit 1; acquire,
  release and abort work for one guest thread. Recursive initialization stops.
- `__atomic_swap`: Bionic `(new_value, address)` ABI, returns old word; aligned
  and bounded, atomic with respect to the single guest thread only.
- `sem_init`: four-byte Bionic layout (`count=value<<1`), maximum 0x3fffffff.
  Invalid values return -1/EINVAL without changing the semaphore. Shared
  semaphores, waiting, posting and thread creation still stop as unsupported.
- `fopen`: only read-only `/proc/cpuinfo` is recognized and returns NULL/ENOENT,
  reflecting PSP's lack of Linux procfs. The donor takes its own absent-file
  fallback. General file/asset access remains unsupported; the host's CPU details
  are never exposed as the guest's architecture.

ABI sources: AOSP Bionic `android-2.3.7_r1` headers, `libc/bionic/semaphore.c`
and `libc/arch-arm/bionic/atomics_arm.S`; ARM C++ ABI guard-object layout.


## JNI bootstrap and lifecycle exports

The bounded export resolver reads public function symbols from SHT_DYNSYM and its
linked string table. It retains the Thumb bit and rejects invalid names, section
indices, targets outside the mapped image or executable file-backed PT_LOAD
ranges, and unaligned ARM entries. Sectionless ELF lookup remains unsupported.

The guest JavaVM/JNIEnv are actual pointer-to-table objects in the bridge arena.
Reserved slots are zero; callable slots point into a separate interpreter trap
range. GetEnv validates the VM/output pointer and returns JNI_EVERSION with a null
output for unsupported versions. GetVersion reports JNI 1.6. The one current guest
thread is attached; attach/detach and unimplemented class/object/member operations trap. Tables reset on image rebinding and do not consume the libc import
registry's remaining slots. No host pointer is placed into guest memory.

`__atomic_inc` and `__atomic_dec` now return the old 32-bit word and wrap the stored
value, matching historical Bionic ARM atomics. `pthread_self` returns thread ID 1,
consistent with existing mutex ownership; `pthread_equal` compares these IDs.
These implement single-thread semantics only; spawning threads is still gated.

All constructors, JNI_OnLoad (returns 0x10004), NativeOnCreate, surface creation and
change, keyboard visibility and window focus complete in the host startup probe.
NativeOnDrawFrame passes EAIO/MainActivity metadata lookup and stops at
the gettimeofday import after EAIO metadata/buffer setup, when assets are mounted. Before initialization completes,
original input callbacks return early; callback returns are not gameplay evidence.


### JNI reference and EAIO metadata support

The bridge reserves 64 opaque guest reference slots, independent from object
identity. FindClass recognizes `com/ea/EAIO/EAIO` (no static initializer) and
`com/ea/blast/MainActivity`. Each lookup returns a local
reference to its class identity. NewGlobalRef/NewLocalRef create independent
handles; deleting a reference invalidates only that handle. Invalid handles and
wrong-kind deletions stop; null reference copying/deletion follows JNI semantics.
IsSameObject compares identity, and GetObjectRefType reports local/global/invalid.
Slots are reused after deletion and reset on image rebinding. Exhaustion stops
explicitly; throwing OutOfMemoryError and automatic JNI local-frame cleanup are
not implemented yet.

GetStaticMethodID matches the exact EAIO class, name and descriptor for native
`Startup(Landroid/content/res/AssetManager;)V` and `Shutdown()V`. IDs are stable
and separate from object references. Startup dispatches to its validated native export when explicitly bound; Shutdown
invocation remains unsupported.
Unknown classes/method signatures trap; no dummy asset manager or successful
file operation is returned. First-draw progress with mounted assets is 341 interpreter steps before
gettimeofday, 11,026 total including constructors and lifecycle.
The JNI arena reservation now uses 68,424 total bridge bytes (including one 64 KiB byte-array buffer);
the total mapped arena remains 30,285,824 bytes with zero allocation failures.

### MainActivity metadata evidence

The supplied APK (SHA256 `00c0a997489f81ab18c2c5e6849e7f2f516e527f8b4c4e13e502f130f8a86bfe`)
contains MainActivity `<clinit>` at DEX code offset `0xc614`: `const/4 v0,0`,
`sput-boolean v0,activityExists`, `return-void`. Its only effect is the default
false value; it does not create an activity. `GetInstance` at `0xc6a8` reads
static `instance`. Java `onCreate` assigns that field before NativeOnCreate.
The host native callback alone does not perform this Java lifecycle.

Exact static `GetInstance()Lcom/ea/blast/MainActivity;` and inherited instance
`getAssets()Landroid/content/res/AssetManager;` metadata have distinct IDs.
Static/instance mismatches, incorrect declaring class and return descriptors
stop. GetInstance invocation now returns null before explicit platform activity
creation, then a fresh local reference to the singleton instance. The platform
calls `ds_imports_start_activity` before NativeOnCreate and supplies its receiver
to activity creation/focus callbacks. This implements the observed singleton
assignment, not Android UI construction or superclass initialization.

CallStaticObjectMethod, V and A variants support this zero-argument method without
reading unused argument pointers. Duplicate platform starts fail; reference-table
exhaustion leaves start retryable. Deleting local handles does not clear the
singleton. Rebinding the image resets it. Activity teardown/recreation within one
image remain unsupported. getAssets requires an explicitly mounted asset root.
The startup-controls trace with assets reaches 11,146 steps; early input callbacks remain
gated by incomplete game initialization.

### Extracted asset backend

`ds_imports_mount_assets(root)` mounts a trusted, extracted APK `assets/` tree
once after JNI preparation. Missing/non-directory roots fail. Rebinding the image
or resetting imports unmounts it and closes all streams. This is a local trusted
data interface, not a filesystem sandbox: the supplied tree must have no symlinks.
Absolute guest names, dot/dot-dot components, backslashes, drive prefixes, empty
components and path truncation are rejected. Only regular files open, read-only.

`DsAssets` owns at most 16 host streams, with no host pointers exposed as guest
handles. Reads are bounded by caller capacity; skip clamps to EOF. The C backend
returns zero at EOF (the future Java InputStream bridge must translate that to
Java's -1). It does not allocate whole asset files. Synthetic tests cover binary
bytes, EOF, independent positions, capacity and reset. A standalone backend check
matched the staged 3,119-byte EAMCore.ini and 15,904,488-byte font using 4 KiB reads.

CallObjectMethod/V/A for getAssets returns independent local handles to one asset
manager identity, requiring the activity instance and exact method ID. Without a
mount it stops. The native probe accepts an optional final assets-directory:

```
probe_guest guest.so startup 100000 18 /path/to/extracted/assets
```

With it, the donor invokes EAIO.Startup through JNI slot142 and reaches
InputStream/asset metadata and allocates its byte-array buffer. Java strings and
AssetManager/InputStream operations are not wired yet; no game-issued file read or rendered frame is claimed. The
PSP diagnostic entry point remains unchanged.

### Native EAIO call bridge

The probe resolves `Java_com_ea_EAIO_EAIO_Startup` through the existing ELF
export validator, then explicitly binds it. The bridge checks image bounds and
ARM/Thumb alignment again. CallStaticVoidMethod/V/A accept only the exact EAIO
Startup ID, EAIO class and a live asset-manager reference. The ARM32 V form
reads one 4-byte object argument; A validates an 8-byte jvalue; direct calls use
r3. Invalid pointers, wrong objects and unbound native targets stop.

Dispatch sets guest r2 to the asset manager and PC/mode to the native entry,
preserving env/class, LR, SP and callee-saved registers. The existing interpreter
executes the native code; return resumes the original JNI caller. Tests execute
real ARM and Thumb BX LR instructions for all three call forms. No nested host
interpreter is introduced. Native targets reset on image rebinding.

EAIO's next GetJavaVM call writes the existing VM pointer to a checked, aligned
guest output and returns JNI_OK. The donor then requests java/io/InputStream;
that metadata is now supported. The current stop is the gettimeofday import.
Automatic JNI local-frame cleanup remains unimplemented as above.

### Stream metadata and bounded byte arrays

Platform adapters expose exact instance-method descriptors for InputStream
read([BII)I, skip(J)J, close()V; AssetManager open(String), openFd(String),
list(String); and AssetFileDescriptor getLength()J. These are metadata entries,
not Android framework class initialization or successful stream operations.
Static/instance, declaring-class and descriptor mismatches stop. GetObjectClass
supports activity and asset-manager objects with independent local references.

NewByteArray uses four reusable buffers of at most 65,536 bytes each within the
existing bridge arena. Buffers allocate lazily, are zeroed before reuse and are
recycled only after the last explicit local/global reference is deleted. No
whole-file allocation is introduced. GetArrayLength and Get/SetByteArrayRegion
validate type, signed-range equivalents, guest pointers and the ARM stack buffer
argument. Region copies handle overlap. Exhaustion, unsupported lengths and
invalid arguments stop; Java exceptions and automatic local-frame cleanup are
still absent. Backing reservations remain until the image is unloaded.

The real donor now reaches gettimeofday at 11,026 startup steps (11,146 with
input callbacks), with 68,424 bridge bytes and unchanged 30,285,824 mapped bytes.
There is still no guest-issued asset read, frame or gameplay verification.
