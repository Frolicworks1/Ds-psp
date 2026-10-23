#include "armv6.h"
#include <string.h>
#include "softfloat.h"
#include "../runtime/imports.h"

#define N_FLAG 0x80000000u
#define Z_FLAG 0x40000000u
#define C_FLAG 0x20000000u
#define V_FLAG 0x10000000u

static uint32_t ror32(uint32_t v, unsigned n) {
    n &= 31u;
    if (!n) return v;
    return (v >> n) | (v << (32u - n));
}

static void set_nz(DsArmv6Cpu *cpu, uint32_t result) {
    cpu->cpsr = (cpu->cpsr & ~(N_FLAG | Z_FLAG)) |
                (result & N_FLAG) | (result == 0 ? Z_FLAG : 0);
}

/* ARM AddWithCarry, also used for subtraction via a + ~b + 1. */
static uint32_t add_with_carry(uint32_t a, uint32_t b, unsigned c,
                               unsigned *carry, unsigned *overflow) {
    uint64_t sum = (uint64_t)a + b + c;
    uint32_t result = (uint32_t)sum;
    *carry = (unsigned)(sum >> 32);
    *overflow = ((~(a ^ b) & (a ^ result)) >> 31) & 1u;
    return result;
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
    if (!image || !image->base || !out || addr < image->load_addr) return -1;
    off = addr - image->load_addr;
    if (off > image->size || image->size - off < 4) return -1;
    memcpy(out, image->base + off, 4);
    return 0;
}

/* VFPv2 register transfers only. Arithmetic and newer register banks remain
 * explicit unsupported instructions. Validate the whole transfer before writes. */
static int vfp_memory(DsArmv6Cpu *cpu, const DsArmv6Image *image, uint32_t insn) {
    unsigned p=(insn>>24)&1, u=(insn>>23)&1, d=(insn>>22)&1;
    unsigned w=(insn>>21)&1, load=(insn>>20)&1, rn=(insn>>16)&15;
    unsigned vd=(insn>>12)&15, dbl=(insn>>8)&1, count=insn&255;
    unsigned first=dbl ? vd*2 : vd*2+d;
    if (dbl && d) return -2; /* D16-D31 require VFPv3. */
    uint32_t base=rn==15 ? (cpu->pc+8u)&~3u : cpu->r[rn];
    uint64_t addr=base, bytes=(uint64_t)count*4, updated=base;
    if (p && !w) { /* VLDR / VSTR immediate offset */
        if (rn==15 && !load) return -2;
        if (!u && bytes>base) return -3;
        addr=u ? (uint64_t)base+bytes : base-bytes;
        count=dbl?2:1;
        bytes=count*4;
    } else { /* VLDM/VSTM increment-after, or decrement-before with writeback */
        if (rn==15 || (!p && !u) || (p && (u || !w)) ||
            !count || (dbl && (count&1)) || first+count>32) return -2;
        if (!u && bytes>base) return -3;
        updated=u ? (uint64_t)base+bytes : base-bytes;
        addr=p?updated:base;
    }
    if (!image || !image->base || (addr&3) || addr<image->load_addr ||
        addr>UINT32_MAX || updated>UINT32_MAX ||
        addr-image->load_addr>image->size || bytes>image->size-(addr-image->load_addr)) return -3;
    uint8_t *memory=image->base+(uint32_t)(addr-image->load_addr);
    if (load) memcpy(cpu->vfp+first,memory,(size_t)bytes);
    else memcpy(memory,cpu->vfp+first,(size_t)bytes);
    if (w) cpu->r[rn]=(uint32_t)updated;
    cpu->pc+=4;
    ++cpu->steps;
    return 0;
}

/* ARMv5 addressing mode 3: halfword, signed and doubleword transfers.
 * Reject unpredictable operands and unaligned halfwords rather than inventing
 * ARMv5 implementation-dependent results. */
static int extra_memory(DsArmv6Cpu *cpu, const DsArmv6Image *image, uint32_t insn) {
    unsigned pre=(insn>>24)&1, up=(insn>>23)&1, imm=(insn>>22)&1;
    unsigned wb=(insn>>21)&1, load=(insn>>20)&1;
    unsigned rn=(insn>>16)&15, rd=(insn>>12)&15, kind=(insn>>5)&3;
    unsigned rm=insn&15, writeback=!pre || wb;
    unsigned dbl=!load && kind>=2;
    if (dbl && ((rd&1) || rd>=14 || (writeback && rn==rd+1))) return -2;
    if (!kind || rd==15 ||
        (!pre && wb) || (writeback && (rn==15 || rn==rd)) ||
        (!imm && ((insn&0xf00) || rm==15 || (writeback && rn==rm)))) return -2;
    uint32_t offset=imm ? ((insn>>4)&0xf0)|(insn&15) : cpu->r[rm];
    uint32_t base=cpu->r[rn];
    uint32_t updated=up ? base+offset : base-offset;
    uint32_t addr=pre ? updated : base;
    unsigned bytes=dbl ? 8 : kind==2 ? 1 : 2;
    if (dbl && (addr&7)) return -3;
    if ((bytes==2 && (addr&1)) || addr<image->load_addr) return -3;
    uint32_t off=addr-image->load_addr;
    if (off>image->size || image->size-off<bytes) return -3;
    uint8_t *memory=image->base+off;
    if (dbl) {
        if (kind==2) {
            memcpy(&cpu->r[rd],memory,4); memcpy(&cpu->r[rd+1],memory+4,4);
        } else {
            memcpy(memory,&cpu->r[rd],4); memcpy(memory+4,&cpu->r[rd+1],4);
        }
    } else if (load) {
        uint32_t value=memory[0];
        if (bytes==2) value|=(uint32_t)memory[1]<<8;
        if (kind==2 && (value&0x80)) value|=0xffffff00u;
        if (kind==3 && (value&0x8000)) value|=0xffff0000u;
        cpu->r[rd]=value;
    } else {
        memory[0]=(uint8_t)cpu->r[rd];
        memory[1]=(uint8_t)(cpu->r[rd]>>8);
    }
    if (writeback) cpu->r[rn]=updated;
    cpu->pc+=4;
    ++cpu->steps;
    return 0;
}

/* Compare raw IEEE binary32 values without relying on host floating-point
 * rounding, NaN handling or signed-zero rules. FZ/vector modes remain gated. */
static int vfp_compare(DsArmv6Cpu *cpu, uint32_t insn) {
    unsigned zero=(insn>>16)&1, signal=(insn>>7)&1;
    unsigned sd=((insn>>12)&15)*2+((insn>>22)&1);
    unsigned sm=(insn&15)*2+((insn>>5)&1);
    if ((zero && (insn&0x2f)) || (cpu->fpscr&0x01370000u)) return -2;
    uint32_t a=cpu->vfp[sd], b=zero ? 0 : cpu->vfp[sm];
    uint32_t ma=a&0x7fffffffu, mb=b&0x7fffffffu, flags;
    int nan_a=ma>0x7f800000u, nan_b=mb>0x7f800000u;
    int invalid=(nan_a && (signal || !(a&0x400000))) ||
                (nan_b && (signal || !(b&0x400000)));
    if (invalid && (cpu->fpscr&0x100)) return -2; /* enabled exception */
    if (nan_a || nan_b) flags=C_FLAG|V_FLAG;
    else if (a==b || (!ma && !mb)) flags=Z_FLAG|C_FLAG;
    else {
        int less=((a^b)&N_FLAG) ? !!(a&N_FLAG) :
            ((a&N_FLAG) ? ma>mb : ma<mb);
        flags=less ? N_FLAG : C_FLAG;
    }
    cpu->fpscr=(cpu->fpscr&0x0fffffffu)|flags|(unsigned)invalid;
    cpu->pc+=4; ++cpu->steps; return 0;
}

static void vfp_begin(DsArmv6Cpu *cpu) {
    static const uint_fast8_t modes[]={softfloat_round_near_even,
        softfloat_round_max,softfloat_round_min,softfloat_round_minMag};
    softfloat_roundingMode=modes[(cpu->fpscr>>22)&3];
    softfloat_detectTininess=softfloat_tininess_beforeRounding;
    softfloat_exceptionFlags=0;
}
static uint32_t vfp_flags(void) {
    unsigned f=softfloat_exceptionFlags;
    return ((f&softfloat_flag_invalid)?1:0)|
        ((f&softfloat_flag_infinite)?2:0)|((f&softfloat_flag_overflow)?4:0)|
        ((f&softfloat_flag_underflow)?8:0)|((f&softfloat_flag_inexact)?16:0);
}
static int vfp_convert_precision(DsArmv6Cpu *cpu,uint32_t insn) {
    unsigned narrow=(insn>>8)&1, d=(insn>>22)&1, m=(insn>>5)&1;
    unsigned vd=(insn>>12)&15, vm=insn&15;
    if ((cpu->fpscr&0x01370000u) || (narrow?m:d)) return -2;
    vfp_begin(cpu);
    uint64_t result;
    if (narrow) {
        float64_t a={(uint64_t)cpu->vfp[vm*2]|((uint64_t)cpu->vfp[vm*2+1]<<32)};
        result=f64_to_f32(a).v;
        if ((cpu->fpscr&0x02000000u) && (result&0x7fffffffu)>0x7f800000u) result=0x7fc00000;
    } else {
        float32_t a={cpu->vfp[vm*2+m]}; result=f32_to_f64(a).v;
        if ((cpu->fpscr&0x02000000u) && (result&UINT64_C(0x7fffffffffffffff))>UINT64_C(0x7ff0000000000000))
            result=UINT64_C(0x7ff8000000000000);
    }
    uint32_t flags=vfp_flags();
    if (flags&(cpu->fpscr>>8)) return -2;
    cpu->vfp[vd*2+(narrow?d:0)]=(uint32_t)result;
    if (!narrow) cpu->vfp[vd*2+1]=(uint32_t)(result>>32);
    cpu->fpscr|=flags; cpu->pc+=4; ++cpu->steps; return 0;
}

static int vfp_from_integer(DsArmv6Cpu *cpu,uint32_t insn) {
    unsigned dbl=(insn>>8)&1, d=(insn>>22)&1, vd=(insn>>12)&15;
    unsigned sm=(insn&15)*2+((insn>>5)&1), sign=(insn>>7)&1;
    if ((dbl && d) || (cpu->fpscr&0x01370000u)) return -2;
    vfp_begin(cpu);
    uint32_t bits=cpu->vfp[sm]; int32_t signed_value;
    memcpy(&signed_value,&bits,4);
    uint64_t result=dbl ? (sign?i32_to_f64(signed_value).v:ui32_to_f64(bits).v) :
        (sign?i32_to_f32(signed_value).v:ui32_to_f32(bits).v);
    uint32_t flags=vfp_flags(); if (flags&(cpu->fpscr>>8)) return -2;
    cpu->vfp[vd*2+(dbl?0:d)]=(uint32_t)result;
    if (dbl) cpu->vfp[vd*2+1]=(uint32_t)(result>>32);
    cpu->fpscr|=flags; cpu->pc+=4; ++cpu->steps; return 0;
}

static int vfp_arithmetic(DsArmv6Cpu *cpu, uint32_t insn) {
    unsigned op=(insn>>20)&11, sub=(insn>>6)&1;
    unsigned sd=((insn>>12)&15)*2+((insn>>22)&1);
    unsigned sn=((insn>>16)&15)*2+((insn>>7)&1);
    unsigned sm=(insn&15)*2+((insn>>5)&1);
    /* Scalar IEEE mode only; FZ and vector modes stop until implemented. */
    if (cpu->fpscr&0x01370000u) return -2;
    if (!(op==0 || (op==2 && !sub) || op==3 || (op==8 && !sub))) return -2;
    vfp_begin(cpu);
    float32_t a={cpu->vfp[sn]}, b={cpu->vfp[sm]}, z;
    if (op==0) {
        /* VMLA/VMLS round the multiply before the accumulate (not fused). */
        float32_t product=f32_mul(a,b), accum={cpu->vfp[sd]};
        if (sub) product.v^=0x80000000u;
        z=f32_add(accum,product);
    } else if (op==2) z=f32_mul(a,b);
    else if (op==8) z=f32_div(a,b);
    else z=sub ? f32_sub(a,b) : f32_add(a,b);
    uint32_t flags=vfp_flags();
    if (flags & (cpu->fpscr>>8)) return -2; /* exception trapping unsupported */
    if ((cpu->fpscr&0x02000000u) && (z.v&0x7fffffffu)>0x7f800000u)
        z.v=0x7fc00000u; /* default NaN */
    cpu->vfp[sd]=z.v; cpu->fpscr|=flags;
    cpu->pc+=4; ++cpu->steps; return 0;
}

static uint32_t operand2(DsArmv6Cpu *cpu, uint32_t insn, unsigned *carry) {
    uint32_t rm = cpu->r[insn & 0xfu];
    unsigned type = (insn >> 5) & 3u;
    unsigned shift_imm = (insn >> 7) & 0x1fu;
    unsigned shift_reg = (insn >> 4) & 1u;

    if (shift_reg) {
        unsigned rs = (insn >> 8) & 0xfu;
        shift_imm = cpu->r[rs] & 0xffu;
        if (!shift_imm) return rm;
    }

    if (type == 0) {
        if (!shift_imm) return rm;
        if (shift_imm >= 32) {
            *carry = shift_imm == 32 ? rm & 1u : 0;
            return 0;
        }
        *carry = (rm >> (32u - shift_imm)) & 1u;
        return rm << shift_imm;
    }
    if (type == 1) {
        if (!shift_imm) shift_imm = 32;
        if (shift_imm >= 32) {
            *carry = shift_imm == 32 ? rm >> 31 : 0;
            return 0;
        }
        *carry = (rm >> (shift_imm - 1u)) & 1u;
        return rm >> shift_imm;
    }
    if (type == 2) {
        if (!shift_imm || shift_imm >= 32) {
            *carry = (rm >> 31) & 1u;
            return (rm & 0x80000000u) ? 0xffffffffu : 0u;
        }
        *carry = (rm >> (shift_imm - 1u)) & 1u;
        return ((int32_t)rm) >> shift_imm;
    }

    if (!shift_imm) {
        *carry = rm & 1u;
        return ((cpu->cpsr & C_FLAG) ? 0x80000000u : 0u) | (rm >> 1);
    }
    *carry = (rm >> ((shift_imm - 1u) & 31u)) & 1u;
    return ror32(rm, shift_imm);
}

void ds_armv6_reset(DsArmv6Cpu *cpu, uint32_t entry) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = entry & ~1u;
    cpu->thumb = entry & 1u;
}

static int thumb_step(DsArmv6Cpu *cpu, const DsArmv6Image *image) {
    uint32_t off, next, target;
    uint16_t hw;
    if (!cpu || !image || !image->base || cpu->pc < image->load_addr) return -1;
    off=cpu->pc-image->load_addr;
    if (off > image->size || image->size-off < 2) return -1;
    memcpy(&hw,image->base+off,2);
    next=cpu->pc+2;
    cpu->r[15]=cpu->pc+4;
    /* MOVS/ADDS/SUBS immediate */
    if ((hw & 0xe000u)==0x2000u) {
        unsigned rd=(hw>>8)&7u; uint32_t imm=hw&0xffu; unsigned op=(hw>>11)&3u;
        uint32_t result;
        unsigned carry=0, overflow=0;
        if(op==0) result=imm;
        else if(op==2) result=add_with_carry(cpu->r[rd],imm,0,&carry,&overflow);
        else result=add_with_carry(cpu->r[rd],~imm,1,&carry,&overflow);
        if(op!=1) cpu->r[rd]=result; /* CMP only changes flags. */
        set_nz(cpu,result);
        if(op!=0) cpu->cpsr=(cpu->cpsr&~(C_FLAG|V_FLAG)) | (carry?C_FLAG:0) | (overflow?V_FLAG:0);
        cpu->pc=next; ++cpu->steps; return 0;
    }
    /* BX/BLX register.  Imported symbols use guest trampoline addresses. */
    if ((hw & 0xff07u)==0x4700u) {
        unsigned rm=(hw>>3)&0xfu;
        int blx=(hw & 0x0080u)!=0;
        target=cpu->r[rm];
        if (blx) cpu->r[14]=next|1u;
        if (ds_import_is_trampoline(target)) {
            int rc=ds_import_call(target,cpu);
            if (rc) return -30;
        } else {
            cpu->pc=target&~1u; cpu->thumb=target&1u;
        }
        ++cpu->steps; return 0;
    }
    /* Conditional branch */
    if ((hw&0xf000u)==0xd000u && ((hw>>8)&0xfu)<0xeu) {
        unsigned cond=(hw>>8)&0xfu; int8_t imm=(int8_t)(hw&0xffu);
        if(cond_ok(cpu->cpsr,cond)) cpu->pc=cpu->pc+4u+(uint32_t)((int32_t)imm*2); else cpu->pc=next;
        ++cpu->steps; return 0;
    }
    /* Unconditional branch */
    if ((hw&0xf800u)==0xe000u) {
        int32_t imm=(int32_t)(hw&0x7ffu); if(imm&0x400) imm|=~0x7ff;
        cpu->pc=cpu->pc+4u+(uint32_t)(imm*2); ++cpu->steps; return 0;
    }
    return -20;
}

int ds_armv6_step(DsArmv6Cpu *cpu, const DsArmv6Image *image) {
    /* ELF PLT stubs can load PC directly, without going through BX/BLX. */
    if (cpu && ds_import_is_trampoline(cpu->pc)) {
        if (ds_import_call(cpu->pc, cpu)) return -30;
        ++cpu->steps;
        return 0;
    }
    if (cpu && cpu->thumb) return thumb_step(cpu,image);
    uint32_t insn, op1, op2, result;
    unsigned cond, rn, rd, carry, overflow = 0;
    uint32_t next_pc;

    if (!cpu || !image || image_read32(image, cpu->pc, &insn) < 0) return -1;
    carry = (cpu->cpsr & C_FLAG) != 0;
    next_pc = cpu->pc + 4u;
    cond = insn >> 28;
    cpu->r[15] = cpu->pc + 8u;

    /* BLX immediate has cond=0xf and must bypass ordinary condition tests. */
    if (cond == 0xfu && (insn & 0xfe000000u) != 0xfa000000u) return -2;
    if (cond != 0xfu && !cond_ok(cpu->cpsr, cond)) {
        cpu->pc = next_pc;
        ++cpu->steps;
        return 0;
    }

    if ((insn&0x0fbf0e50u)==0x0eb80a40u)
        return vfp_from_integer(cpu,insn);
    if ((insn&0x0fbf0ed0u)==0x0eb70ac0u)
        return vfp_convert_precision(cpu,insn);
    if ((insn&0x0f000e10u)==0x0e000a00u && !(insn&0x100u) &&
        (((insn>>20)&11)==0 || ((insn>>20)&11)==2 || ((insn>>20)&11)==3 || ((insn>>20)&11)==8))
        return vfp_arithmetic(cpu,insn);
    if ((insn&0x0fbf0f50u)==0x0eb40a40u ||
        (insn&0x0fbf0f50u)==0x0eb50a40u)
        return vfp_compare(cpu,insn);
    if ((insn&0x0fff0fffu)==0x0ef10a10u) { /* VMRS */
        unsigned rt=(insn>>12)&15;
        if (rt==15) cpu->cpsr=(cpu->cpsr&0x0fffffffu)|(cpu->fpscr&0xf0000000u);
        else cpu->r[rt]=cpu->fpscr;
        cpu->pc=next_pc; ++cpu->steps; return 0;
    }

    /* VMOV between one ARM register and one raw VFP single register. */
    if ((insn & 0x0fe00f7fu) == 0x0e000a10u) {
        unsigned rt=(insn>>12)&15, sn=((insn>>16)&15)*2+((insn>>7)&1);
        if (rt==15) return -2;
        if (insn&0x00100000u) cpu->r[rt]=cpu->vfp[sn];
        else cpu->vfp[sn]=cpu->r[rt];
        cpu->pc=next_pc; ++cpu->steps; return 0;
    }

    if ((insn&0x0fe00fd0u)==0x0c400b10u) { /* VMOV two ARM regs <-> Dn */
        unsigned rt=(insn>>12)&15, rt2=(insn>>16)&15, dm=insn&15;
        unsigned load=(insn>>20)&1;
        if ((insn&0x20) || rt==15 || rt2==15 || (load && rt==rt2)) return -2;
        if (load) { cpu->r[rt]=cpu->vfp[dm*2]; cpu->r[rt2]=cpu->vfp[dm*2+1]; }
        else { cpu->vfp[dm*2]=cpu->r[rt]; cpu->vfp[dm*2+1]=cpu->r[rt2]; }
        cpu->pc=next_pc; ++cpu->steps; return 0;
    }

    if ((insn & 0x0e000e00u) == 0x0c000a00u)
        return vfp_memory(cpu, image, insn);

    if ((insn & 0x0ffffff0u) == 0x012fff10u ||
        (insn & 0x0ffffff0u) == 0x012fff30u) {
        uint32_t target = cpu->r[insn & 0xfu];
        int blx = ((insn & 0x0ffffff0u) == 0x012fff30u);
        if (blx) cpu->r[14] = cpu->pc + 4u;
        if (ds_import_is_trampoline(target)) {
            int rc=ds_import_call(target,cpu);
            if (rc) return -30;
        } else {
            cpu->pc = target & ~1u;
            cpu->thumb = target & 1u;
        }
        ++cpu->steps;
        return 0;
    }

    /* ARM BL: save return address in LR, then branch. */
    if (cond != 0xfu && (insn & 0x0f000000u) == 0x0b000000u) {
        int32_t off = (int32_t)(insn & 0x00ffffffu);
        if (off & 0x00800000) off |= (int32_t)0xff000000u;
        cpu->r[14] = cpu->pc + 4u;
        cpu->pc = cpu->pc + 8u + ((uint32_t)off << 2);
        ++cpu->steps;
        return 0;
    }

    /* ARM BLX immediate (ARMv5T+ encoding). */
    if ((insn & 0xfe000000u) == 0xfa000000u) {
        int32_t off = (int32_t)(insn & 0x00ffffffu);
        unsigned h = (insn >> 24) & 1u;
        if (off & 0x00800000) off |= (int32_t)0xff000000u;
        cpu->r[14] = cpu->pc + 4u;
        cpu->pc = (cpu->pc + 8u + ((uint32_t)off << 2) + (h << 1)) & ~1u;
        cpu->thumb = 1u;
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

    /* LDM/STM block transfer, including PUSH/POP stack forms. */
    if ((insn & 0x0e000000u) == 0x08000000u) {
        unsigned rn2=(insn>>16)&0xfu;
        unsigned pre=(insn>>24)&1u, up=(insn>>23)&1u, wb=(insn>>21)&1u;
        unsigned load=(insn>>20)&1u;
        uint32_t list=insn&0xffffu;
        unsigned count=0;
        for (unsigned i=0;i<16;++i) if (list&(1u<<i)) ++count;
        if (!count) return -2;
        uint32_t base=cpu->r[rn2];
        uint32_t addr;
        if (up) addr=base+(pre?4u:0u);
        else addr=base-(pre?4u:0u)-4u*(count-1u);
        for (unsigned i=0;i<16;++i) if (list&(1u<<i)) {
            if (addr < image->load_addr) return -3;
            uint32_t mo=addr-image->load_addr;
            if (mo>image->size || image->size-mo<4) return -3;
            if (load) {
                uint32_t v; memcpy(&v,image->base+mo,4);
                if (i==15u) { cpu->pc=v&~1u; cpu->thumb=v&1u; }
                else cpu->r[i]=v;
            } else {
                uint32_t v=(i==15u)?cpu->pc+8u:cpu->r[i];
                memcpy(image->base+mo,&v,4);
            }
            addr+=4u;
        }
        if (wb) cpu->r[rn2]=up ? base+4u*count : base-4u*count;
        if (!(load && (list&0x8000u))) cpu->pc=next_pc;
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
            /* Addressing mode 2 permits immediate shifts, not register shifts. */
            if (insn & 0x10u) return -2;
            unsigned ignored_carry = carry;
            off = operand2(cpu, insn, &ignored_carry);
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
        uint32_t total_size=image->size;
        if (mem_off >= total_size || total_size - mem_off < (byte ? 1u : 4u)) return -3;
        if (load) {
            if (byte) value = image->base[mem_off];
            else memcpy(&value, image->base + mem_off, 4);
            if (rd2 == 15u) {
                cpu->pc = value & ~1u;
                cpu->thumb = value & 1u;
            }
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

    if ((insn&0x0fff0ff0u)==0x016f0f10u) { /* CLZ */
        unsigned dest=(insn>>12)&15, source=insn&15;
        if (dest==15 || source==15) return -2;
        uint32_t value=cpu->r[source]; unsigned count=0;
        while (count<32 && !(value&0x80000000u)) { ++count; value<<=1; }
        cpu->r[dest]=count; cpu->pc=next_pc; ++cpu->steps; return 0;
    }

    /* UMULL/UMLAL/SMULL/SMLAL: snapshot operands before writing either half. */
    if ((insn&0x0f8000f0u)==0x00800090u) {
        unsigned hi=(insn>>16)&15, lo=(insn>>12)&15;
        unsigned rs=(insn>>8)&15, rm=insn&15;
        if (hi==15 || lo==15 || rs==15 || rm==15 || hi==lo) return -2;
        uint64_t product;
        if (insn&0x00400000u) {
            int64_t a=(cpu->r[rm]&0x80000000u) ? (int64_t)cpu->r[rm]-0x100000000LL : cpu->r[rm];
            int64_t b=(cpu->r[rs]&0x80000000u) ? (int64_t)cpu->r[rs]-0x100000000LL : cpu->r[rs];
            product=(uint64_t)(a*b);
        } else product=(uint64_t)cpu->r[rm]*cpu->r[rs];
        if (insn&0x00200000u) product+=((uint64_t)cpu->r[hi]<<32)|cpu->r[lo];
        cpu->r[lo]=(uint32_t)product; cpu->r[hi]=(uint32_t)(product>>32);
        if (insn&0x00100000u)
            cpu->cpsr=(cpu->cpsr&~(N_FLAG|Z_FLAG))|(cpu->r[hi]&N_FLAG)|(!product?Z_FLAG:0);
        cpu->pc=next_pc; ++cpu->steps; return 0;
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

    if ((insn & 0x0e000090u) == 0x00000090u && (insn & 0x60u))
        return extra_memory(cpu, image, insn);

    /* BX was handled above; SWI is surfaced as a host/runtime trap. */
    if ((insn & 0x0f000000u) == 0x0f000000u) return -4;

    if ((insn & 0x0c000000u) != 0) return -2;
    /* Do not reinterpret halfword transfers or PSR operations as arithmetic. */
    if (!(insn & 0x02000000u) && (insn & 0x90u) == 0x90u) return -2;
    unsigned opcode = (insn >> 21) & 0xfu;
    int flags_only = opcode >= 8u && opcode <= 11u;
    if (flags_only && !(insn & 0x00100000u)) return -2;

    rn = (insn >> 16) & 0xfu;
    rd = (insn >> 12) & 0xfu;
    if (!flags_only && rd == 15u && (insn & 0x00100000u)) return -2;
    op1 = cpu->r[rn];

    if (insn & 0x02000000u) {
        unsigned rotate = ((insn >> 8) & 0xfu) * 2u;
        op2 = ror32(insn & 0xffu, rotate);
        if (rotate) carry = (op2 >> 31) & 1u;
    } else {
        op2 = operand2(cpu, insn, &carry);
    }

    unsigned input_carry = (cpu->cpsr & C_FLAG) != 0;
    int arithmetic = (opcode >= 2u && opcode <= 7u) || opcode == 10u || opcode == 11u;
    switch (opcode) {
    case 0x0: result = op1 & op2; break;
    case 0x1: result = op1 ^ op2; break;
    case 0x2: result = add_with_carry(op1, ~op2, 1, &carry, &overflow); break;
    case 0x3: result = add_with_carry(op2, ~op1, 1, &carry, &overflow); break;
    case 0x4: result = add_with_carry(op1, op2, 0, &carry, &overflow); break;
    case 0x5: result = add_with_carry(op1, op2, input_carry, &carry, &overflow); break;
    case 0x6: result = add_with_carry(op1, ~op2, input_carry, &carry, &overflow); break;
    case 0x7: result = add_with_carry(op2, ~op1, input_carry, &carry, &overflow); break;
    case 0x8: result = op1 & op2; break;
    case 0x9: result = op1 ^ op2; break;
    case 0xa: result = add_with_carry(op1, ~op2, 1, &carry, &overflow); break;
    case 0xb: result = add_with_carry(op1, op2, 0, &carry, &overflow); break;
    case 0xc: result = op1 | op2; break;
    case 0xd: result = op2; break;
    case 0xe: result = op1 & ~op2; break;
    case 0xf: result = ~op2; break;
    default: return -2;
    }

    if (flags_only) {
        cpu->pc = next_pc;
    } else if (rd == 15u) {
        cpu->pc = result & ~1u;
    } else {
        cpu->r[rd] = result;
        cpu->pc = next_pc;
    }

    if (insn & 0x00100000u) {
        set_nz(cpu, result);
        cpu->cpsr &= ~C_FLAG;
        if (carry) cpu->cpsr |= C_FLAG;
        if (arithmetic) cpu->cpsr = (cpu->cpsr & ~V_FLAG) | (overflow ? V_FLAG : 0);
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
    DsArmv6Image image = {0};
    ds_armv6_reset(&cpu, 0x1000u);
    image.base = (uint8_t *)(uintptr_t)program;
    image.size = sizeof(program);
    image.load_addr = 0x1000u;

    if (ds_armv6_step(&cpu, &image) != 0 || cpu.r[0] != 7) return -1;
    if (ds_armv6_step(&cpu, &image) != 0 || cpu.r[1] != 12) return -2;
    if (ds_armv6_step(&cpu, &image) != 0 || !(cpu.cpsr & Z_FLAG)) return -3;
    if (ds_armv6_step(&cpu, &image) != 0 || cpu.pc != 0x1014u) return -4;
    return 0;
}
