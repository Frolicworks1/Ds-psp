cmake_minimum_required(VERSION 3.16)
project(dspsp_host_checks C)
enable_testing()
add_executable(test_armv6 test_armv6.c ../src/cpu/armv6.c)
target_compile_options(test_armv6 PRIVATE -Wall -Wextra -Werror -Wpedantic)
option(DSPSP_SANITIZE "Enable address/undefined behavior sanitizers" OFF)
if(DSPSP_SANITIZE)
    target_compile_options(test_armv6 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_armv6 PRIVATE -fsanitize=address,undefined)
endif()
foreach(group selftest fetch_bounds flags_only arithmetic_flags carry_arithmetic
              register_shifts rrx arm_branches blx_immediate thumb_immediates
              thumb_branches memory_bounds unsupported)
    add_test(NAME armv6_${group} COMMAND test_armv6 ${group})
endforeach()

# Optional local investigation; proprietary inputs are never required by CTest/CI.
add_executable(probe_guest probe_guest.c ../src/cpu/armv6.c ../src/runtime/arm_loader.c)
target_include_directories(probe_guest PRIVATE host)
target_compile_options(probe_guest PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(DSPSP_SANITIZE)
    target_compile_options(probe_guest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(probe_guest PRIVATE -fsanitize=address,undefined)
endif()
