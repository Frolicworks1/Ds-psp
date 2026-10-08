#include "armv6.h"
#include <string.h>

#define N_FLAG 0x80000000u
#define Z_FLAG 0x40000000u
#define C_FLAG 0x20000000u
#define V_FLAG 0x10000000u

static uint32_t ror32(uint32_t v, unsigned n) {
    if (!n) return v;
    return (v >> n) | (v << (32u - n));
}

static int cond_ok(uint32_t cpsr, unsigned cond) {
    int n = (cpsr & N_FLAG) != 0;
    int z = (cpsr & Z_FLAG) != 0;
    int c = (cpsr & C_FLAG) != 0;
    int v = (cpsr & V_FLAG) != 0;

    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xa: return n == v;
    case 0xb: return n != v;
    case 0xc: return !z && (n == v);
    case 0xd: return z || (n != v);
    case 0xe: return 1;
    default: return 0;
    }
}

static int image_read32(const DsArmv6Image *image, uint32_t addr, uint32_t *out) {
    uint32_t off;
    if (!image || !out || addr < image->load_addr) return -1;
    off = addr - image->load_addr;
    if (off > image->size || image->size - off < 4) return -1;
    memcpy(out, image->base + off, 4);
    return 0;
}

static uint32_t operand2(DsArmv6Cpu *cpu, uint32_t insn, unsigned *carry) {
    uint32_t rm = cpu->r[insn & 0xfu];
    unsigned type = (insn >> 5) & 3u;
    unsigned shift_imm = (insn >> 7) & 0x1fu;
    unsigned shift_reg = (insn >> 4) & 1u;

    if (shift_reg) {
        unsigned rs = (insn >> 8) & 0xfu;
        shift_imm = cpu->r[rs] & 0xffu;
    }

    if (type == 0) {
        if (!shift_imm) return rm;
        *carry = (rm >> (32u - shift_imm)) & 1u;
        return rm << shift_imm;
    }
    if (type == 1) {
        if (!shift_imm) {
            *carry = (rm >> 31) & 1u;
            return 0;
        }
        *carry = (rm >> (shift_imm - 1u)) & 1u;
        return rm >> shift_imm;
    }
    if (type == 2) {
        if (!shift_imm) {
            *carry = (rm >> 31) & 1u;
            return (rm & 0x80000000u) ? 0xffffffffu : 0u;
        }
        *carry = (rm >> (shift_imm - 1u)) & 1u;
        return ((int32_t)rm) >> shift_imm;
    }

    if (!shift_imm) {
        *carry = rm & 1u;
        return (cpu->cpsr & C_FLAG) ? 0x80000000u : 0u;
    }
    *carry = (rm >> (shift_imm - 1u)) & 1u;
    return ror32(rm, shift_imm);
}

void ds_armv6_reset(DsArmv6Cpu *cpu, uint32_t entry) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = entry;
}

int ds_armv6_step(DsArmv6Cpu *cpu, const DsArmv6Image *image) {
    uint32_t insn, op1, op2, result;
    unsigned cond, rn, rd, carry = (cpu->cpsr & C_FLAG) != 0;
    uint32_t next_pc;

    if (!cpu || !image || image_read32(image, cpu->pc, &insn) < 0) return -1;
    next_pc = cpu->pc + 4u;
    cond = insn >> 28;
    cpu->r[15] = cpu->pc + 8u;

    if (!cond_ok(cpu->cpsr, cond)) {
        cpu->pc = next_pc;
        ++cpu->steps;
        return 0;
    }

    if ((insn & 0x0ffffff0u) == 0x012fff10u) {
        uint32_t target = cpu->r[insn & 0xfu];
        cpu->pc = target & ~1u;
        ++cpu->steps;
        return 0;
    }

    if ((insn & 0x0e000000u) == 0x0a000000u) {
        int32_t off = (int32_t)(insn & 0x00ffffffu);
        if (off & 0x00800000) off |= (int32_t)0xff000000u;
        cpu->pc = cpu->pc + 8u + ((uint32_t)off << 2);
        ++cpu->steps;
        return 0;
    }

    /* Single data transfer: LDR/STR (word/byte), immediate or register offset. */
    if ((insn & 0x0c000000u) == 0x04000000u) {
        unsigned rn2 = (insn >> 16) & 0xfu;
        unsigned rd2 = (insn >> 12) & 0xfu;
        unsigned byte = (insn >> 22) & 1u;
        unsigned pre = (insn >> 24) & 1u;
        unsigned up = (insn >> 23) & 1u;
        unsigned wb = (insn >> 21) & 1u;
        unsigned load = (insn >> 20) & 1u;
        uint32_t off;
        if (insn & 0x02000000u) {
            unsigned rm = insn & 0xfu;
            off = cpu->r[rm];
            unsigned st = (insn >> 5) & 3u;
            unsigned sh = (insn >> 7) & 0x1fu;
            if (st == 0) off = sh ? off << sh : off;
            else if (st == 1) off = sh ? off >> sh : 0;
            else if (st == 2) off = sh ? (uint32_t)((int32_t)off >> sh)
                                      : ((off & 0x80000000u) ? 0xffffffffu : 0);
            else off = sh ? ror32(off, sh) : ((cpu->cpsr & C_FLAG) ? 0x80000000u : 0);
        } else {
            off = insn & 0xfffu;
        }
        uint32_t base_addr = cpu->r[rn2];
        uint32_t addr = pre ? (up ? base_addr + off : base_addr - off) : base_addr;
        uint32_t updated = up ? base_addr + off : base_addr - off;
        uint32_t value = 0;
        uint32_t mem_off;
        if (addr < image->load_addr) return -3;
        mem_off = addr - image->load_addr;
        if (mem_off >= image->size || image->size - mem_off < (byte ? 1u : 4u)) return -3;
        if (load) {
            if (byte) value = image->base[mem_off];
            else memcpy(&value, image->base + mem_off, 4);
            if (rd2 == 15u) cpu->pc = value & ~1u;
            else cpu->r[rd2] = value;
        } else {
            value = (rd2 == 15u) ? cpu->pc + 8u : cpu->r[rd2];
            if (byte) image->base[mem_off] = (uint8_t)value;
            else memcpy(image->base + mem_off, &value, 4);
        }
        if (!pre || wb) cpu->r[rn2] = updated;
        if (load && rd2 == 15u) cpu->pc = value & ~1u;
        else cpu->pc = next_pc;
        ++cpu->steps;
        return 0;
    }

    /* MUL/MLA, with optional flag update. */
    if ((insn & 0x0fc000f0u) == 0x00000090u) {
        unsigned rd2 = (insn >> 16) & 0xfu;
        unsigned rn2 = (insn >> 12) & 0xfu;
        unsigned rs2 = (insn >> 8) & 0xfu;
        unsigned rm2 = insn & 0xfu;
        uint32_t value = cpu->r[rm2] * cpu->r[rs2];
        if (insn & 0x00200000u) value += cpu->r[rn2];
        cpu->r[rd2] = value;
        if (insn & 0x00100000u) {
            cpu->cpsr &= ~(N_FLAG | Z_FLAG);
            if (value & 0x80000000u) cpu->cpsr |= N_FLAG;
            if (!value) cpu->cpsr |= Z_FLAG;
        }
        cpu->pc = next_pc;
        ++cpu->steps;
        return 0;
    }

    /* BX was handled above; SWI is surfaced as a host/runtime trap. */
    if ((insn & 0x0f000000u) == 0x0f000000u) return -4;

    if ((insn & 0x0c000000u) != 0) return -2;

    rn = (insn >> 16) & 0xfu;
    rd = (insn >> 12) & 0xfu;
    op1 = cpu->r[rn];

    if (insn & 0x02000000u) {
        unsigned rotate = ((insn >> 8) & 0xfu) * 2u;
        op2 = ror32(insn & 0xffu, rotate);
        if (rotate) carry = (op2 >> 31) & 1u;
    } else {
        op2 = operand2(cpu, insn, &carry);
    }

    switch ((insn >> 21) & 0xfu) {
    case 0x0: result = op1 & op2; break;
    case 0x1: result = op1 ^ op2; break;
    case 0x2: result = op1 - op2; break;
    case 0x3: result = op2 - op1; break;
    case 0x4: result = op1 + op2; break;
    case 0x5: result = op1 + op2 + (cpu->cpsr & C_FLAG ? 1u : 0u); break;
    case 0x6: result = op1 - op2 - (cpu->cpsr & C_FLAG ? 0u : 1u); break;
    case 0x7: result = op2 - op1 - (cpu->cpsr & C_FLAG ? 0u : 1u); break;
    case 0x8: result = op1 & op2; break;
    case 0x9: result = op1 ^ op2; break;
    case 0xa: result = op1 - op2; break;
    case 0xb: result = op1 + op2; break;
    case 0xc: result = op1 | op2; break;
    case 0xd: result = op2; break;
    case 0xe: result = op1 & ~op2; break;
    case 0xf: result = ~op2; break;
    default: return -2;
    }

    if (rd == 15u) {
        cpu->pc = result & ~1u;
    } else {
        cpu->r[rd] = result;
        cpu->pc = next_pc;
    }

    if (insn & 0x00100000u) {
        cpu->cpsr &= ~(N_FLAG | Z_FLAG | C_FLAG);
        if (result & 0x80000000u) cpu->cpsr |= N_FLAG;
        if (result == 0) cpu->cpsr |= Z_FLAG;
        if (carry) cpu->cpsr |= C_FLAG;
    }

    ++cpu->steps;
    return 0;
}

int ds_armv6_run(DsArmv6Cpu *cpu, const DsArmv6Image *image, uint32_t max_steps) {
    uint32_t i;
    for (i = 0; i < max_steps; ++i) {
        int rc = ds_armv6_step(cpu, image);
        if (rc) return rc;
    }
    return 0;
}

int ds_armv6_selftest(void) {
    static const uint32_t program[] = {
        0xe3a00007u, /* mov r0,#7 */
        0xe2801005u, /* add r1,r0,#5 */
        0xe351000cu, /* cmp r1,#12 */
        0x0a000000u  /* beq +0 */
    };
    DsArmv6Cpu cpu;
    DsArmv6Image image;
    ds_armv6_reset(&cpu, 0x1000u);
    image.base = (uint8_t *)(uintptr_t)program;
    image.size = sizeof(program);
    image.load_addr = 0x1000u;

    if (ds_armv6_step(&cpu, &image) != 0 || cpu.r[0] != 7) return -1;
    if (ds_armv6_step(&cpu, &image) != 0 || cpu.r[1] != 12) return -2;
    if (ds_armv6_step(&cpu, &image) != 0 || !(cpu.cpsr & Z_FLAG)) return -3;
    if (ds_armv6_step(&cpu, &image) != 0 || cpu.pc != 0x1008u) return -4;
    return 0;
}
