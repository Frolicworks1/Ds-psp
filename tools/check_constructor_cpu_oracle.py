#!/usr/bin/env python3
"""Optional independent constructor CPU instruction check; requires Unicorn 2.1.4 and a host C compiler.
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


rng = random.Random(20261024)
# Include NaNs, infinities, denormals, cancellation, signed zero and rounding ties.
edges=[0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,
       0x3f800001,0x33800000,0x7f7fffff,0xff7fffff,0x7f800000,
       0xff800000,0x7fc01234,0x7f801234,0xffc05678]
opcodes=[0xee300a01,0xee300a41,0xee200a01,0xee800a01,0xee000a01,0xee000a41,
         0xeeb40a41,0xeeb40ac1,0xeeb50a40,0xeeb50ac0,
         0xeeb70ac1,0xeeb70bc1,0xeeb80a41,0xeeb80ac1,0xeeb80b41,0xeeb80bc1,
         0xee000a10,0xee100a10,0xec410b10,0xec510b10,0xeef10a10,0xeef1fa10,
         0xe0832190,0xe0a32190,0xe0c32190,0xe0e32190,0xe0932190,0xe0d32190,
         0xe16f2f1c,0xe3a024bf]
vectors=[(op,mode,a,b) for op in opcodes for mode in range(4)
         for a,b in [(a,b) for a in edges for b in edges]]
vectors += [(rng.choice(opcodes),rng.randrange(4),rng.getrandbits(32),rng.getrandbits(32))
            for _ in range(2000)]

with tempfile.TemporaryDirectory(prefix='ds-cpu-oracle-') as tmp:
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
    for opcode, mode, a, b in vectors:
        raw = bytearray(rng.randbytes(4096))
        struct.pack_into('<I', raw, 0, opcode)
        buf = (c.c_uint8 * len(raw)).from_buffer(raw)
        image = Image(buf, len(raw), 0x1000)
        cpu = CPU()
        cpu.pc, cpu.cpsr = 0x1000, 0x10
        cpu.fpscr = mode << 22
        machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        machine.mem_map(0x1000, 4096)
        machine.mem_write(0x1000, bytes(raw))
        machine.reg_write(arm.UC_ARM_REG_CPSR, 0x10)
        machine.reg_write(arm.UC_ARM_REG_FPEXC, 0x40000000)
        machine.reg_write(arm.UC_ARM_REG_FPSCR, cpu.fpscr)
        regs = [getattr(arm, f'UC_ARM_REG_R{i}') for i in range(13)] + [arm.UC_ARM_REG_SP, arm.UC_ARM_REG_LR]
        for i, reg in enumerate(regs):
            cpu.r[i] = a if i%2 else b
            machine.reg_write(reg, cpu.r[i])
        for i in range(16):
            low, high = (a,b) if i%2 else (b,a)
            cpu.vfp[2*i], cpu.vfp[2*i+1] = low, high
            machine.reg_write(getattr(arm, f'UC_ARM_REG_D{i}'), low | high << 32)
        machine.emu_start(0x1000, 0x1004, count=1)
        assert lib.ds_armv6_step(c.byref(cpu), c.byref(image)) == 0, hex(opcode)
        assert cpu.fpscr == machine.reg_read(arm.UC_ARM_REG_FPSCR), (hex(opcode), mode, hex(a), hex(b), hex(cpu.fpscr), hex(machine.reg_read(arm.UC_ARM_REG_FPSCR)))
        assert cpu.cpsr & 0xf0000000 == machine.reg_read(arm.UC_ARM_REG_CPSR)&0xf0000000, hex(opcode)
        assert bytes(raw) == bytes(machine.mem_read(0x1000, 4096)), hex(opcode)
        assert cpu.pc == machine.reg_read(arm.UC_ARM_REG_PC) and cpu.steps == 1, hex(opcode)
        for i, reg in enumerate(regs):
            assert cpu.r[i] == machine.reg_read(reg), (hex(opcode), mode, hex(a), hex(b), i, hex(actual) if 'actual' in locals() else '')
        for i in range(16):
            actual = cpu.vfp[2*i] | cpu.vfp[2*i+1] << 32
            assert actual == machine.reg_read(getattr(arm, f'UC_ARM_REG_D{i}')), (hex(opcode), mode, hex(a), hex(b), i, hex(actual) if 'actual' in locals() else '')
print(f'{len(vectors)} CPU/VFP vectors match Unicorn registers, flags and memory')
