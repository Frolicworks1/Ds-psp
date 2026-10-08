cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{PSPDEV})
    message(FATAL_ERROR "PSPDEV is not set")
endif()

set(CMAKE_C_COMPILER "$ENV{PSPDEV}/bin/psp-gcc" CACHE FILEPATH "" FORCE)

project(deadspace_psp C)

add_executable(EBOOT.ELF
    src/main.c
    src/runtime/donor_elf.c
)

target_compile_options(EBOOT.ELF PRIVATE -O2 -G0 -Wall -Wextra)
target_link_options(EBOOT.ELF PRIVATE -G0)
target_link_libraries(EBOOT.ELF pspgu pspdisplay pspctrl pspkernel)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PARAM.SFO"
    COMMAND mksfoex "Dead Space PSP" "${CMAKE_CURRENT_BINARY_DIR}/PARAM.SFO"
    DEPENDS EBOOT.ELF
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/EBOOT.PBP"
    COMMAND psp-fixup-imports "${CMAKE_CURRENT_BINARY_DIR}/EBOOT.ELF"
    COMMAND pack-pbp "${CMAKE_CURRENT_BINARY_DIR}/EBOOT.PBP"
            "${CMAKE_CURRENT_BINARY_DIR}/PARAM.SFO"
            NULL NULL NULL NULL
            "${CMAKE_CURRENT_BINARY_DIR}/EBOOT.ELF" NULL
    DEPENDS EBOOT.ELF "${CMAKE_CURRENT_BINARY_DIR}/PARAM.SFO"
)

add_custom_target(EBOOT ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/EBOOT.PBP")
