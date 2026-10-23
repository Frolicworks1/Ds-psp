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
first-constructor probe now reaches `__aeabi_atexit` at trampoline `0xf0000248` after
1,639 successful steps, then stops. No complete constructor, JNI initialization
or gameplay is established. The donor requested mappings of 16,781,312 and 16,777,216 bytes; both returned
ENOMEM under the current budget. Continued execution does not prove that its
allocator is usable. Investigate the larger memory requirement before gameplay.

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
Non-fixed address hints may be ignored. A 512-page ownership bitmap implements
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
constructor enumeration. No successful donor mmap is recorded yet.
