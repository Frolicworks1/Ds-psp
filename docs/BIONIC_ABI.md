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

## Execution checkpoint

All seven required data globals now resolve for the recorded donor. The bounded
first-constructor probe reaches `pthread_mutexattr_init` at trampoline
`0xf000057c` after 191 successful steps, then stops. No complete constructor,
JNI initialization or gameplay is established. ELF PLT stubs that load PC directly
now dispatch registered imports before instruction fetch; synthetic tests cover
this path independently of the donor.
