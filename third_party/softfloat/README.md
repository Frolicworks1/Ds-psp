# Berkeley SoftFloat 3e subset

Upstream: https://github.com/ucb-bar/berkeley-softfloat-3
Revision: 32eb3295a3c814c1551f94ebc29eed1dec3cb3b1.
Unmodified source/header subset, ARM-VFPv2 specialization; local platform.h
selects little endian, portable integer helpers and 64/32 division. License in
COPYING.txt must accompany distributed binaries. Only binary32 operations needed
by the interpreter are compiled. Global SoftFloat state is used synchronously by
the existing single guest thread; concurrent CPU execution is not supported.
