#!/usr/bin/env python3
"""Optional independent VFP transfer check; requires Unicorn 2.1.4 and a host C compiler.
No donor data or PSP SDK required. The regular sanitizer tests have no Unicorn dependency.
"""
import ctypes as c
import pathlib
import random
import struct
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn import arm_const as arm

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CPU(c.Structure):
    _fields_ = [('r', c.c_uint32 * 16), ('cpsr', c.c_uint32), ('pc', c.c_uint32),
                ('steps', c.c_uint32), ('thumb', c.c_uint32), ('vfp', c.c_uint32 * 32), ('fpscr', c.c_uint32)]


class Image(c.Structure):
    _fields_ = [('base', c.POINTER(c.c_uint8)), ('size', c.c_uint32), ('load_addr', c.c_uint32)]


rng = random.Random(20261023)
vectors = [0xed2d8b06, 0xecbd8b06, 0xed9f9aa4, 0xed849a0f, 0xed910b02,
           0xed010b02, 0xedd10a01, 0xecb10a04, 0xeca10a04, 0xed9f0a00]
for _ in range(256):
    double = rng.randrange(2)
    first = rng.randrange(16 if double else 32)
    vd, d = (first, 0) if double else (first // 2, first & 1)
    load = rng.randrange(2)
    if rng.randrange(2):
        p, u, w, imm = 1, rng.randrange(2), 0, rng.randrange(64)
    else:
        p = rng.randrange(2)
        u, w = 1 - p, 1 if p else rng.randrange(2)
        imm = rng.randint(1, (16 if double else 32) - first) * (2 if double else 1)
    vectors.append(0xec000a00 | p << 24 | u << 23 | d << 22 | w << 21 |
                   load << 20 | 1 << 16 | vd << 12 | double << 8 | imm)

with tempfile.TemporaryDirectory(prefix='ds-vfp-') as tmp:
    stub = pathlib.Path(tmp) / 'stub.c'
    stub.write_text('#include <stdint.h>\nstruct DsArmv6Cpu;\n'
                    'int ds_import_is_trampoline(uint32_t a){(void)a;return 0;}\n'
                    'int ds_import_call(uint32_t a,struct DsArmv6Cpu*c){(void)a;(void)c;return -1;}\n')
    libpath = pathlib.Path(tmp) / 'cpu.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-label',
                    str(ROOT / 'src/cpu/armv6.c'), str(stub),
                    '-I'+str(ROOT/'third_party/softfloat/include'), '-I'+str(ROOT/'third_party/softfloat'),
                    *map(str, sorted((ROOT/'third_party/softfloat').glob('*.c'))), '-o', str(libpath)], check=True)
    lib = c.CDLL(str(libpath))
    lib.ds_armv6_step.argtypes = [c.POINTER(CPU), c.POINTER(Image)]
    lib.ds_armv6_step.restype = c.c_int
    for opcode in vectors:
        raw = bytearray(rng.randbytes(4096))
        struct.pack_into('<I', raw, 0, opcode)
        buf = (c.c_uint8 * len(raw)).from_buffer(raw)
        image = Image(buf, len(raw), 0x1000)
        cpu = CPU()
        cpu.pc, cpu.cpsr = 0x1000, 0x10
        machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        machine.mem_map(0x1000, 4096)
        machine.mem_write(0x1000, bytes(raw))
        machine.reg_write(arm.UC_ARM_REG_CPSR, 0x10)
        machine.reg_write(arm.UC_ARM_REG_FPEXC, 0x40000000)
        regs = [getattr(arm, f'UC_ARM_REG_R{i}') for i in range(13)] + [arm.UC_ARM_REG_SP, arm.UC_ARM_REG_LR]
        for i, reg in enumerate(regs):
            cpu.r[i] = 0x1800
            machine.reg_write(reg, cpu.r[i])
        for i in range(16):
            low, high = rng.getrandbits(32), rng.getrandbits(32)
            cpu.vfp[2*i], cpu.vfp[2*i+1] = low, high
            machine.reg_write(getattr(arm, f'UC_ARM_REG_D{i}'), low | high << 32)
        machine.emu_start(0x1000, 0x1004, count=1)
        assert lib.ds_armv6_step(c.byref(cpu), c.byref(image)) == 0, hex(opcode)
        assert bytes(raw) == bytes(machine.mem_read(0x1000, 4096)), hex(opcode)
        assert cpu.pc == machine.reg_read(arm.UC_ARM_REG_PC) and cpu.steps == 1, hex(opcode)
        for i, reg in enumerate(regs):
            assert cpu.r[i] == machine.reg_read(reg), (hex(opcode), i)
        for i in range(16):
            actual = cpu.vfp[2*i] | cpu.vfp[2*i+1] << 32
            assert actual == machine.reg_read(getattr(arm, f'UC_ARM_REG_D{i}')), (hex(opcode), i)
print(f'{len(vectors)} VFP transfer vectors match Unicorn registers, memory and writeback')
