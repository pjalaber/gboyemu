#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <SDL.h>
#include "z80.h"
#include "mmu.h"
#include "gpu.h"
#include "rom.h"
#include "interrupt.h"

static struct z80_cpu z80;

static inline uint8_t Z_GET(void)
{
	return ((z80.F & 0x80) >> 7);
}

static inline uint8_t N_GET(void)
{
	return ((z80.F & 0x40) >> 6);
}

static inline uint8_t H_GET(void)
{
	return ((z80.F & 0x20) >> 5);
}

static inline uint8_t C_GET(void)
{
	return ((z80.F & 0x10) >> 4);
}

static inline void Z_SET(uint8_t value)
{
	if ( value )
		z80.F |= 0x80;
	else
		z80.F &= ~0x80;
}

static inline void N_SET(uint8_t value)
{
	if ( value )
		z80.F |= 0x40;
	else
		z80.F &= ~0x40;
}

static inline void H_SET(uint8_t value)
{
	if ( value )
		z80.F |= 0x20;
	else
		z80.F &= ~0x20;
}

static inline void C_SET(uint8_t value)
{
	if ( value )
		z80.F |= 0x10;
	else
		z80.F &= ~0x10;
}

static void z80_dump_regs(void)
{
	fprintf(stderr, "A:0x%02X B:0x%02X C:0x%02X D:0x%02X E:0x%02X H:0x%02X L:0x%02X "
		"F:[Z:%u N:%u H:%u C:%u] SP:0x%04X PC:0x%04X\n",
		z80.A, z80.B, z80.C, z80.D, z80.E, z80.H, z80.L,
		Z_GET(), N_GET(), H_GET(), C_GET(), z80.SP, z80.PC);
}

int32_t z80_init(void)
{
	memset(&z80, 0, sizeof(z80));
	z80.PC = 0x100;
        z80.SP = 0xFFFE;
	z80.BC = 0x0013;
	z80.DE = 0x00D8;
	z80.HL = 0x014D;
	z80.AF = 0x01B0;

	return 0;
}

uint32_t z80_halted(void)
{
	return z80.halted;
}

uint32_t z80_stopped(void)
{
	return z80.stopped;
}

void z80_resume_halt(void)
{
	z80.halted = 0;
}

void z80_resume_stop(void)
{
	z80.stopped = 0;
}

static inline void z80_push16(uint16_t v1)
{
	z80.SP -= 2;
	mmu_write_mem16(z80.SP, v1);
}

static inline uint16_t z80_pop16(void)
{
	uint16_t u16;
	u16 = mmu_read_mem16(z80.SP);
	z80.SP += 2;
	return u16;
}

void z80_call(uint16_t addr)
{
	z80_push16(z80.PC);
	z80.PC = addr;
}

static inline uint8_t z80_add8(uint8_t v1, uint8_t v2)
{
	uint16_t u16;
	u16 = v1 + v2;
	N_SET(0);
	C_SET(u16 > 0xFF);
	H_SET(0x0F - (v1 & 0x0F) < (v2 & 0x0F));
	v1 = u16;
	Z_SET(v1 == 0);
	return v1;
}

static inline uint8_t z80_sub8(uint8_t v1, uint8_t v2)
{
	N_SET(1);
	Z_SET(v1 == v2);
	H_SET((v1 & 0xF) < (v2 & 0xF));
	C_SET(v1 < v2);
	v1 -= v2;
	return v1;
}

static inline uint16_t z80_add16(uint16_t v1, uint16_t v2)
{
	uint16_t u16;
	u16 = v1 + v2;
	C_SET(0xFFFF - v1 < v2);
	H_SET(0x0FFF - (v1 & 0x0FFF) < (v2 & 0x0FFF));
	N_SET(0);
	return u16;
}

static inline uint16_t z80_add_16_8(uint16_t v1, uint8_t v2)
{
        C_SET(0xFF - (v1 & 0x00FF) < v2);
	H_SET(0x0F - (v1 & 0x0F) < (v2 & 0x0F));
	N_SET(0);
	Z_SET(0);
        return v1 + (int8_t)v2;
}

static inline uint8_t z80_xor8(uint8_t v1, uint8_t v2)
{
	v1 ^= v2;
	Z_SET(v1 == 0);
	N_SET(0);
	H_SET(0);
	C_SET(0);
	return v1;
}

static inline uint8_t z80_or8(uint8_t v1, uint8_t v2)
{
	v1 |= v2;
	Z_SET(v1 == 0);
	N_SET(0);
	H_SET(0);
	C_SET(0);
	return v1;
}

static inline uint8_t z80_and8(uint8_t v1, uint8_t v2)
{
	v1 &= v2;
	Z_SET(v1 == 0);
	N_SET(0);
	H_SET(1);
	C_SET(0);
	return v1;
}

static inline uint8_t z80_adc8(uint8_t v1, uint8_t v2)
{
	uint8_t temp, temp2;
	uint8_t carry_set = 0;
	uint8_t half_set = 0;

	temp = v1 + C_GET();

	if ( 0x0F - (v1 & 0x0F) < (C_GET() & 0x0F) )
		half_set = 1;
	if ( 0xFF - v1 < C_GET() )
		carry_set = 1;

	temp2 = temp + v2;
	if ( 0x0F - (temp & 0x0F) < (v2 & 0x0F) )
		half_set = 1;
	if ( 0xFF - temp < v2 )
		carry_set = 1;

	H_SET(half_set);
	C_SET(carry_set);
	Z_SET(temp2 == 0);
	N_SET(0);

	return temp2;
}

static inline uint8_t z80_sbc8(uint8_t v1, uint8_t v2)
{
	uint8_t temp, temp2;
	uint8_t carry_set = 0;
	uint8_t half_set = 0;

	temp = v1 - C_GET();

	if ( v1 < C_GET() )
		carry_set = 1;
	if ( (v1 & 0x0F) < (C_GET() & 0x0F) )
		half_set = 1;

	temp2 = temp - v2;

	if ( temp < v2 )
		carry_set = 1;
	if ( (temp & 0x0F) < (v2 & 0x0F) )
		half_set = 1;

	H_SET(half_set);
	C_SET(carry_set);
	Z_SET(temp2 == 0);
	N_SET(1);

	return temp2;
}


static inline uint8_t z80_inc8(uint8_t v1)
{
	v1++;
	H_SET((v1 & 0xF) == 0);
	Z_SET(v1 == 0);
	N_SET(0);
	return v1;
}

static inline uint8_t z80_dec8(uint8_t v1)
{
	v1--;
	H_SET((v1 & 0xF) == 0xF);
	Z_SET(v1 == 0);
	N_SET(1);
	return v1;
}

static inline void z80_cp8(uint8_t v1, uint8_t v2)
{
	N_SET(1);
	Z_SET(v1 == v2);
	H_SET((v1 & 0xF) < (v2 & 0xF));
	C_SET(v1 < v2);
}

static inline uint8_t z80_rl8(uint8_t v1)
{
	uint8_t u8;
	u8 = C_GET();
	C_SET((v1 & 0x80) >> 7);
	v1 = (v1 << 1) + u8;
	Z_SET(v1 == 0);
	N_SET(0);
	H_SET(0);
	return v1;
}

static inline uint8_t z80_rlc8(uint8_t v1)
{
	C_SET((v1 & 0x80) >> 7);
	N_SET(0);
	H_SET(0);
	v1 = (v1 << 1) + C_GET();
	Z_SET(v1 == 0);
	return v1;
}

static inline uint8_t z80_rrc8(uint8_t v1)
{
	C_SET(v1 & 0x1);
	N_SET(0);
	H_SET(0);
	v1 = (v1 >> 1) + (C_GET() << 7);
	Z_SET(v1 == 0);
	return v1;
}

static inline uint8_t z80_swap8(uint8_t v1)
{
	v1 = ((v1 & 0x0F) << 4) | ((v1 & 0xF0) >> 4);
	Z_SET(v1 == 0);
	N_SET(0);
	H_SET(0);
	C_SET(0);
	return v1;
}

static inline uint8_t z80_rr8(uint8_t v1)
{
	uint8_t u8;
	u8 = C_GET();
	C_SET(v1 & 0x1);
	v1 = (v1 >> 1) | (u8 << 7);
	Z_SET(v1 == 0);
	N_SET(0);
	H_SET(0);
	return v1;
}

static inline uint8_t z80_sla8(uint8_t v1)
{
	C_SET((v1 & 0x80) >> 7);
	N_SET(0);
	H_SET(0);
	v1 = v1 << 1;
	Z_SET(v1 == 0);
	return v1;
}

static inline uint8_t z80_sra8(uint8_t v1)
{
	C_SET(v1 & 0x1);
	N_SET(0);
	H_SET(0);
	v1 = (v1 & 0x80) | (v1 >> 1);
	Z_SET(v1 == 0);
	return v1;
}

static inline uint8_t z80_srl8(uint8_t v1)
{
	C_SET(v1 & 0x1);
	N_SET(0);
	H_SET(0);
	v1 = v1 >> 1;
	Z_SET(v1 == 0);
	return v1;
}

static inline void z80_bit8(uint8_t v1, uint8_t bit)
{
	assert(bit <= 7);
	Z_SET((v1 & (1 << bit)) == 0);
	N_SET(0);
	H_SET(1);
}

static inline uint8_t z80_set8(uint8_t v1, uint8_t bit)
{
	assert(bit <= 7);
	return v1 | (1 << bit);
}

static inline uint8_t z80_res8(uint8_t v1, uint8_t bit)
{
	assert(bit <= 7);
	return v1 & ~(1 << bit);
}

static inline uint8_t z80_daa8(uint8_t v1)
{
	uint32_t u32 = v1;

	if ( N_GET() == 0 ) {
		if ( H_GET() || ((u32 & 0x0F) > 9) )
			u32 += 6;
		if ( C_GET() || (u32 > 0x9F) )
			u32 += 0x60;
	}
	else {
		if ( H_GET() )
			u32 = (u32 - 6) & 0xFF;
		if ( C_GET() )
			u32 -= 0x60;
	}

	if ( u32 & 0x100 )
	{
		C_SET(1);
	}
	H_SET(0);
	u32 &= 0xff;
	Z_SET(u32 == 0);
	return u32;
}

uint32_t z80_next_opcode(uint32_t disassemble)
{
	uint8_t op1, op2, op3;
	uint16_t PC_saved;
	uint8_t rom_bank;
	uint32_t cycles, instr_size;
	uint32_t enable_interrupt, disable_interrupt;

#define disassemble(fmt, ...)						\
	do {								\
		if ( disassemble ) \
		{							\
			switch ( instr_size ) {				\
				case 1:					\
				fprintf(stderr, "%04X:%02X    %02X                ", PC_saved, rom_bank, op1); \
				break;					\
				case 2:					\
				fprintf(stderr, "%04X:%02X    %02X %02X             ", PC_saved, rom_bank, op1, op2); \
				break;					\
				case 3:					\
				fprintf(stderr, "%04X:%02X    %02X %02X %02X          ", PC_saved, rom_bank, op1, op2, op3); \
				break;					\
				default:				\
				assert(0);				\
			}						\
			fprintf(stderr, fmt "\n", ##__VA_ARGS__);	\
			z80_dump_regs();				\
		}							\
	} while (0)

#define pc_cyc(PC_INC, CYCLES)					\
	do {							\
		assert(PC_INC <= 3);				\
		if ( PC_INC >= 2 )				\
			op2 = mmu_read_mem8(z80.PC + 1);	\
		if ( PC_INC == 3 )				\
			op3 = mmu_read_mem8(z80.PC + 2);	\
		z80.PC += PC_INC;				\
		instr_size = PC_INC;				\
		cycles = CYCLES;				\
	} while (0)

	if ( z80.halted || z80.stopped )
		return 4;

	rom_bank = rom_get_rom_bank();
	PC_saved = z80.PC;
	op1 = mmu_read_mem8(z80.PC);
	op2 = op3 = 0;
	enable_interrupt = disable_interrupt = 0;

	switch ( op1 )
	{
		case 0x00:
		pc_cyc(1, 4);
		disassemble("NOP");
		break;

		case 0x06:
		pc_cyc(2, 8);
		z80.B = op2;
		disassemble("LD B,0x%02X", op2);
		break;

		case 0x0E:
		pc_cyc(2, 8);
		z80.C = op2;
		disassemble("LD C,0x%02X", op2);
		break;

		case 0x16:
		pc_cyc(2, 8);
		z80.D = op2;
		disassemble("LD D,0x%02X", op2);
		break;

		case 0x1E:
		pc_cyc(2, 8);
		z80.E = op2;
		disassemble("LD E,0x%02X", op2);
		break;

		case 0x26:
		pc_cyc(2, 8);
		z80.H = op2;
		disassemble("LD H,0x%02X", op2);
		break;

		case 0x2E:
		pc_cyc(2, 8);
		z80.L = op2;
		disassemble("LD L,0x%02X", op2);
		break;

		case 0x7F:
		pc_cyc(1, 4);
		z80.A = z80.A;
		disassemble("LD A,A");
		break;

		case 0x78:
		pc_cyc(1, 4);
		z80.A = z80.B;
		disassemble("LD A,B");
		break;

		case 0x79:
		pc_cyc(1, 4);
		z80.A = z80.C;
		disassemble("LD A,C");
		break;

		case 0x7A:
		pc_cyc(1, 4);
		z80.A = z80.D;
		disassemble("LD A,D");
		break;

		case 0x7B:
		pc_cyc(1, 4);
		z80.A = z80.E;
		disassemble("LD A,E");
		break;

		case 0x7C:
		pc_cyc(1, 4);
		z80.A = z80.H;
		disassemble("LD A,H");
		break;

		case 0x7D:
		pc_cyc(1, 4);
		z80.A = z80.L;
		disassemble("LD A,L");
		break;

		case 0x7E:
		pc_cyc(1, 8);
		z80.A = mmu_read_mem8(z80.HL);
		disassemble("LD A,(HL)");
		break;

		case 0x0A:
		pc_cyc(1, 8);
		z80.A = mmu_read_mem8(z80.BC);
		disassemble("LD A,(BC)");
		break;

		case 0x1A:
		pc_cyc(1, 8);
		z80.A = mmu_read_mem8(z80.DE);
		disassemble("LD A,(DE)");
		break;

		case 0x40:
		pc_cyc(1, 4);
		z80.B = z80.B;
		disassemble("LD B,B");
		break;

		case 0x41:
		pc_cyc(1, 4);
		z80.B = z80.C;
		disassemble("LD B,C");
		break;

		case 0x42:
		pc_cyc(1, 4);
		z80.B = z80.D;
		disassemble("LD B,D");
		break;

		case 0x43:
		pc_cyc(1, 4);
		z80.B = z80.E;
		disassemble("LD B,E");
		break;

		case 0x44:
		pc_cyc(1, 4);
		z80.B = z80.H;
		disassemble("LD B,H");
		break;

		case 0x45:
		pc_cyc(1, 4);
		z80.B = z80.L;
		disassemble("LD B,L");
		break;

		case 0x46:
		pc_cyc(1, 8);
		z80.B = mmu_read_mem8(z80.HL);
		disassemble("LD B,(HL)");
		break;

		case 0x47:
		pc_cyc(1, 4);
		z80.B = z80.A;
		disassemble("LD B,A");
		break;

		case 0x48:
		pc_cyc(1, 4);
		z80.C = z80.B;
		disassemble("LD C,B");
		break;

		case 0x49:
		pc_cyc(1, 4);
		z80.C = z80.C;
		disassemble("LD C,C");
		break;

		case 0x4A:
		pc_cyc(1, 4);
		z80.C = z80.D;
		disassemble("LD C,D");
		break;

		case 0x4B:
		pc_cyc(1, 4);
		z80.C = z80.E;
		disassemble("LD C,E");
		break;

		case 0x4C:
		pc_cyc(1, 4);
		z80.C = z80.H;
		disassemble("LD C,H");
		break;

		case 0x4D:
		pc_cyc(1, 4);
		z80.C = z80.L;
		disassemble("LD C,L");
		break;

		case 0x4E:
		pc_cyc(1, 8);
		z80.C = mmu_read_mem8(z80.HL);
		disassemble("LD C,(HL)");
		break;

		case 0x4F:
		pc_cyc(1, 4);
		z80.C = z80.A;
		disassemble("LD C,A");
		break;

		case 0x50:
		pc_cyc(1, 4);
		z80.D = z80.B;
		disassemble("LD D,B");
		break;

		case 0x51:
		pc_cyc(1, 4);
		z80.D = z80.C;
		disassemble("LD D,C");
		break;

		case 0x52:
		pc_cyc(1, 4);
		z80.D = z80.D;
		disassemble("LD D,D");
		break;

		case 0x53:
		pc_cyc(1, 4);
		z80.D = z80.E;
		disassemble("LD D,E");
		break;

		case 0x54:
		pc_cyc(1, 4);
		z80.D = z80.H;
		disassemble("LD D,H");
		break;

		case 0x55:
		pc_cyc(1, 4);
		z80.D = z80.L;
		disassemble("LD D,L");
		break;

		case 0x56:
		pc_cyc(1, 8);
		z80.D = mmu_read_mem8(z80.HL);
		disassemble("LD D,(HL)");
		break;

		case 0x57:
		pc_cyc(1, 4);
		z80.D = z80.A;
		disassemble("LD D,A");
		break;

		case 0x58:
		pc_cyc(1, 4);
		z80.E = z80.B;
		disassemble("LD E,B");
		break;

		case 0x59:
		pc_cyc(1, 4);
		z80.E = z80.C;
		disassemble("LD E,C");
		break;

		case 0x5A:
		pc_cyc(1, 4);
		z80.E = z80.D;
		disassemble("LD E,D");
		break;

		case 0x5B:
		pc_cyc(1, 4);
		z80.E = z80.E;
		disassemble("LD E,E");
		break;

		case 0x5C:
		pc_cyc(1, 4);
		z80.E = z80.H;
		disassemble("LD E,H");
		break;

		case 0x5D:
		pc_cyc(1, 4);
		z80.E = z80.L;
		disassemble("LD E,L");
		break;

		case 0x5E:
		pc_cyc(1, 8);
		z80.E = mmu_read_mem8(z80.HL);
		disassemble("LD E,(HL)");
		break;

		case 0x5F:
		pc_cyc(1, 4);
		z80.E = z80.A;
		disassemble("LD E,A");
		break;

		case 0x60:
		pc_cyc(1, 4);
		z80.H = z80.B;
		disassemble("LD H,B");
		break;

		case 0x61:
		pc_cyc(1, 4);
		z80.H = z80.C;
		disassemble("LD H,C");
		break;

		case 0x62:
		pc_cyc(1, 4);
		z80.H = z80.D;
		disassemble("LD H,D");
		break;

		case 0x63:
		pc_cyc(1, 4);
		z80.H = z80.E;
		disassemble("LD H,E");
		break;

		case 0x64:
		pc_cyc(1, 4);
		z80.H = z80.H;
		disassemble("LD H,H");
		break;

		case 0x65:
		pc_cyc(1, 4);
		z80.H = z80.L;
		disassemble("LD H,L");
		break;

		case 0x66:
		pc_cyc(1, 8);
		z80.H = mmu_read_mem8(z80.HL);
		disassemble("LD H,(HL)");
		break;

		case 0x67:
		pc_cyc(1, 4);
		z80.H = z80.A;
		disassemble("LD H,A");
		break;

		case 0x68:
		pc_cyc(1, 4);
		z80.L = z80.B;
		disassemble("LD L,B");
		break;

		case 0x69:
		pc_cyc(1, 4);
		z80.L = z80.C;
		disassemble("LD L,C");
		break;

		case 0x6A:
		pc_cyc(1, 4);
		z80.L = z80.D;
		disassemble("LD L,D");
		break;

		case 0x6B:
		pc_cyc(1, 4);
		z80.L = z80.E;
		disassemble("LD L,E");
		break;

		case 0x6C:
		pc_cyc(1, 4);
		z80.L = z80.H;
		disassemble("LD L,H");
		break;

		case 0x6D:
		pc_cyc(1, 4);
		z80.L = z80.L;
		disassemble("LD L,L");
		break;

		case 0x6E:
		pc_cyc(1, 8);
		z80.L = mmu_read_mem8(z80.HL);
		disassemble("LD L,(HL)");
		break;

		case 0x6F:
		pc_cyc(1, 4);
		z80.L = z80.A;
		disassemble("LD L,A");
		break;

		case 0x70:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.B);
		disassemble("LD (HL),B");
		break;

		case 0x71:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.C);
		disassemble("LD (HL),C");
		break;

		case 0x72:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.D);
		disassemble("LD (HL),D");
		break;

		case 0x73:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.E);
		disassemble("LD (HL),E");
		break;

		case 0x74:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.H);
		disassemble("LD (HL),H");
		break;

		case 0x75:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.L);
		disassemble("LD (HL),L");
		break;

		case 0x36:
		pc_cyc(2, 12);
		mmu_write_mem8(z80.HL, op2);
		disassemble("LD (HL),0x%02X", op2);
		break;

		case 0xFA:
		pc_cyc(3, 16);
		z80.A = mmu_read_mem8((op3 << 8) + op2);
		disassemble("LD A,(0x%02X%02X)", op3, op2);
		break;

		case 0x3E:
		pc_cyc(2, 8);
		z80.A = op2;
		disassemble("LD A,0x%02X", op2);
		break;

		case 0x02:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.BC, z80.A);
		disassemble("LD (BC),A");
		break;

		case 0x12:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.DE, z80.A);
		disassemble("LD (DE),A");
		break;

		case 0x77:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.A);
		disassemble("LD (HL),A");
		break;

		case 0xEA:
		pc_cyc(3, 8);
		mmu_write_mem8((op3 << 8) + op2, z80.A);
		disassemble("LD (0x%02X%02X),A", op3, op2);
		break;

		case 0xF2:
		pc_cyc(1, 8);
		z80.A = mmu_read_mem8(0xFF00 + z80.C);
		disassemble("LD A,(0xFF00+C)");
		break;

		case 0xE2:
		pc_cyc(1, 8);
		mmu_write_mem8(0xFF00 + z80.C, z80.A);
		disassemble("LD (0xFF00+C),A");
		break;

		case 0x3A:
		pc_cyc(1, 8);
		z80.A = mmu_read_mem8(z80.HL);
		z80.HL--;
		disassemble("LDD A,(HL)");
		break;

		case 0x32:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.A);
		z80.HL--;
		disassemble("LDD (HL),A");
		break;

		case 0x2A:
		pc_cyc(1, 8);
		z80.A = mmu_read_mem8(z80.HL);
		z80.HL++;
		disassemble("LDI A,(HL)");
		break;

		case 0x22:
		pc_cyc(1, 8);
		mmu_write_mem8(z80.HL, z80.A);
		z80.HL++;
		disassemble("LDI (HL),A");
		break;

		case 0xE0:
		pc_cyc(2, 12);
		mmu_write_mem8(0xFF00 + op2, z80.A);
		disassemble("LD (0xFF%02X),A", op2);
		break;

		case 0xF0:
		pc_cyc(2, 12);
		z80.A = mmu_read_mem8(0xFF00 + op2);
		disassemble("LD A,(0xFF%02X)", op2);
		break;

		case 0x01:
		pc_cyc(3, 12);
		z80.BC = (op3 << 8) + op2;
		disassemble("LD BC,0x%04X", z80.BC);
		break;

		case 0x11:
		pc_cyc(3, 12);
		z80.DE = (op3 << 8) + op2;
		disassemble("LD DE,0x%04X", z80.DE);
		break;

		case 0x21:
		pc_cyc(3, 12);
		z80.HL = (op3 << 8) + op2;
		disassemble("LD HL,0x%04X", z80.HL);
		break;

		case 0x31:
		pc_cyc(3, 12);
		z80.SP = (op3 << 8) + op2;
		disassemble("LD SP,0x%04X", z80.SP);
		break;

		case 0xF9:
		pc_cyc(1, 8);
		z80.SP = z80.HL;
		disassemble("LD SP,HL");
		break;

		case 0xF8:
		pc_cyc(2, 12);
		z80.HL = z80_add_16_8(z80.SP, op2);
		disassemble("LDHL SP,0x%02X", op2);
		break;

		case 0x08:
		pc_cyc(3, 20);
		mmu_write_mem16((op3 << 8) + op2, z80.SP);
		disassemble("LD (0x%04X),SP", (op3 << 8) + op2);
		break;

		case 0xF5:
		pc_cyc(1, 16);
		/* F lower nibble bits must be always 0
		 */
		z80.F &= 0xF0;
		z80_push16(z80.AF);
		disassemble("PUSH AF");
		break;

		case 0xC5:
		pc_cyc(1, 16);
		z80_push16(z80.BC);
		disassemble("PUSH BC");
		break;

		case 0xD5:
		pc_cyc(1, 16);
		z80_push16(z80.DE);
		disassemble("PUSH DE");
		break;

		case 0xE5:
		pc_cyc(1, 16);
		z80_push16(z80.HL);
		disassemble("PUSH HL");
		break;

		case 0xF1:
		pc_cyc(1, 12);
		z80.AF = z80_pop16();
		disassemble("POP AF");
		break;

		case 0xC1:
		pc_cyc(1, 12);
		z80.BC = z80_pop16();
		disassemble("POP BC");
		break;

		case 0xD1:
		pc_cyc(1, 12);
		z80.DE = z80_pop16();
		disassemble("POP DE");
		break;

		case 0xE1:
		pc_cyc(1, 12);
		z80.HL = z80_pop16();
		disassemble("POP HL");
		break;

		case 0xC3:
		pc_cyc(3, 16);
		z80.PC = (op3 << 8) + op2;
		disassemble("JP 0x%04X", z80.PC);
		break;

		case 0x18:
		pc_cyc(2, 12);
		z80.PC += (int8_t)op2;
		disassemble("JR 0x%02X", op2);
		break;

		case 0xC2:
		pc_cyc(3, (Z_GET() == 0) ? 16 : 12);
		if ( Z_GET() == 0 )
			z80.PC = (op3 << 8) + op2;
		disassemble("JP NZ,0x%04X", (op3 << 8) + op2);
		break;

		case 0xCA:
		pc_cyc(3, Z_GET() ? 16 : 12);
		if ( Z_GET() )
			z80.PC = (op3 << 8) + op2;
		disassemble("JP Z,0x%04X", (op3 << 8) + op2);
		break;

		case 0xD2:
		pc_cyc(3, (C_GET() == 0) ? 16 : 12);
		if ( C_GET() == 0 )
			z80.PC = (op3 << 8) + op2;
		disassemble("JP NC,0x%04X", (op3 << 8) + op2);
		break;

		case 0xDA:
		pc_cyc(3, C_GET() ? 16 : 12);
		if ( C_GET() )
			z80.PC = (op3 << 8) + op2;
		disassemble("JP C,0x%04X", (op3 << 8) + op2);
		break;

		case 0xE9:
		pc_cyc(1, 4);
		z80.PC = z80.HL;
		disassemble("JP HL");
		break;

		case 0xF3:
		pc_cyc(1, 4);
		disable_interrupt = 1;
		disassemble("DI");
		break;

		case 0xFB:
		pc_cyc(1, 4);
		enable_interrupt = 1;
		disassemble("EI");
		break;

		case 0x09:
		pc_cyc(1, 8);
		z80.HL = z80_add16(z80.HL, z80.BC);
		disassemble("ADD HL,BC");
		break;

		case 0x19:
		pc_cyc(1, 8);
		z80.HL = z80_add16(z80.HL, z80.DE);
		disassemble("ADD HL,DE");
		break;

		case 0x29:
		pc_cyc(1, 8);
		z80.HL = z80_add16(z80.HL, z80.HL);
		disassemble("ADD HL,HL");
		break;

		case 0x39:
		pc_cyc(1, 8);
		z80.HL = z80_add16(z80.HL, z80.SP);
		disassemble("ADD HL,SP");
		break;

		case 0xE8:
		pc_cyc(2, 4);
		z80.SP = z80_add_16_8(z80.SP, op2);
		disassemble("ADD SP,0x%02X", op2);
		break;

		case 0x87:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.A);
		disassemble("ADD A,A");
		break;

		case 0x80:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.B);
		disassemble("ADD A,B");
		break;

		case 0x81:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.C);
		disassemble("ADD A,C");
		break;

		case 0x82:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.D);
		disassemble("ADD A,D");
		break;

		case 0x83:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.E);
		disassemble("ADD A,E");
		break;

		case 0x84:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.H);
		disassemble("ADD A,H");
		break;

		case 0x85:
		pc_cyc(1, 4);
		z80.A = z80_add8(z80.A, z80.L);
		disassemble("ADD A,L");
		break;

		case 0x86:
		pc_cyc(1, 8);
		z80.A = z80_add8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("ADD A,(HL)");
		break;

		case 0xC6:
		pc_cyc(2, 8);
		z80.A = z80_add8(z80.A, op2);
		disassemble("ADD A,0x%02X", op2);
		break;

		case 0x8F:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.A);
		disassemble("ADC A,A");
		break;

		case 0x88:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.B);
		disassemble("ADC A,B");
		break;

		case 0x89:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.C);
		disassemble("ADC A,C");
		break;

		case 0x8A:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.D);
		disassemble("ADC A,D");
		break;

		case 0x8B:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.E);
		disassemble("ADC A,E");
		break;

		case 0x8C:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.H);
		disassemble("ADC A,H");
		break;

		case 0x8D:
		pc_cyc(1, 4);
		z80.A = z80_adc8(z80.A, z80.L);
		disassemble("ADC A,L");
		break;

		case 0x8E:
		pc_cyc(1, 8);
		z80.A = z80_adc8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("ADC A,(HL)");
		break;

		case 0xCE:
		pc_cyc(2, 8);
		z80.A = z80_adc8(z80.A, op2);
		disassemble("ADC A,0x%02X", op2);
		break;

		case 0x90:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.B);
		disassemble("SUB A,B");
		break;

		case 0x91:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.C);
		disassemble("SUB A,C");
		break;

		case 0x92:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.D);
		disassemble("SUB A,D");
		break;

		case 0x93:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.E);
		disassemble("SUB A,E");
		break;

		case 0x94:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.H);
		disassemble("SUB A,H");
		break;

		case 0x95:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.L);
		disassemble("SUB A,L");
		break;

		case 0x96:
		pc_cyc(1, 8);
		z80.A = z80_sub8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("SUB A,(HL)");
		break;

		case 0x97:
		pc_cyc(1, 4);
		z80.A = z80_sub8(z80.A, z80.A);
		disassemble("SUB A,A");
		break;

		case 0xD6:
		pc_cyc(2, 8);
		z80.A = z80_sub8(z80.A, op2);
		disassemble("SUB A,0x%02X", op2);
		break;

		case 0x9F:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.A);
		disassemble("SBC A,A");
		break;

		case 0x98:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.B);
		disassemble("SBC A,B");
		break;

		case 0x99:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.C);
		disassemble("SBC A,C");
		break;

		case 0x9A:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.D);
		disassemble("SBC A,D");
		break;

		case 0x9B:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.E);
		disassemble("SBC A,E");
		break;

		case 0x9C:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.H);
		disassemble("SBC A,H");
		break;

		case 0x9D:
		pc_cyc(1, 4);
		z80.A = z80_sbc8(z80.A, z80.L);
		disassemble("SBC A,L");
		break;

		case 0x9E:
		pc_cyc(1, 8);
		z80.A = z80_sbc8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("SBC A,(HL)");
		break;

		case 0xDE:
		pc_cyc(2, 8);
		z80.A = z80_sbc8(z80.A, op2);
		disassemble("SBC A,0x%02X", op2);
		break;

		case 0xA7:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.A);
		disassemble("AND A,A");
		break;

		case 0xA0:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.B);
		disassemble("AND A,B");
		break;

		case 0xA1:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.C);
		disassemble("AND A,C");
		break;

		case 0xA2:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.D);
		disassemble("AND A,D");
		break;

		case 0xA3:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.E);
		disassemble("AND A,E");
		break;

		case 0xA4:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.H);
		disassemble("AND A,H");
		break;

		case 0xA5:
		pc_cyc(1, 4);
		z80.A = z80_and8(z80.A, z80.L);
		disassemble("AND A,L");
		break;

		case 0xA6:
		pc_cyc(1, 8);
		z80.A = z80_and8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("AND A,(HL)");
		break;

		case 0xE6:
		pc_cyc(2, 8);
		z80.A = z80_and8(z80.A, op2);
		disassemble("AND A,0x%02X", op2);
		break;

		case 0xB7:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.A);
		disassemble("OR A,A");
		break;

		case 0xB0:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.B);
		disassemble("OR A,B");
		break;

		case 0xB1:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.C);
		disassemble("OR A,C");
		break;

		case 0xB2:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.D);
		disassemble("OR A,D");
		break;

		case 0xB3:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.E);
		disassemble("OR A,E");
		break;

		case 0xB4:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.H);
		disassemble("OR A,H");
		break;

		case 0xB5:
		pc_cyc(1, 4);
		z80.A = z80_or8(z80.A, z80.L);
		disassemble("OR A,L");
		break;

		case 0xB6:
		pc_cyc(1, 8);
		z80.A = z80_or8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("OR A,(HL)");
		break;

		case 0xF6:
		pc_cyc(2, 8);
		z80.A = z80_or8(z80.A, op2);
		disassemble("OR A,0x%02x", op2);
		break;

		case 0xAF:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.A);
		disassemble("XOR A,A");
		break;

		case 0xA8:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.B);
		disassemble("XOR A,B");
		break;

		case 0xA9:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.C);
		disassemble("XOR A,C");
		break;

		case 0xAA:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.D);
		disassemble("XOR A,D");
		break;

		case 0xAB:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.E);
		disassemble("XOR A,E");
		break;

		case 0xAC:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.H);
		disassemble("XOR A,H");
		break;

		case 0xAD:
		pc_cyc(1, 4);
		z80.A = z80_xor8(z80.A, z80.L);
		disassemble("XOR A,L");
		break;

		case 0xAE:
		pc_cyc(1, 8);
		z80.A = z80_xor8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("XOR A,(HL)");
		break;

		case 0xEE:
		pc_cyc(2, 8);
		z80.A = z80_xor8(z80.A, op2);
		disassemble("XOR A,0x%02X", op2);
		break;

		case 0xBF:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.A);
		disassemble("CP A,A");
		break;

		case 0xB8:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.B);
		disassemble("CP A,B");
		break;

		case 0xB9:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.C);
		disassemble("CP A,C");
		break;

		case 0xBA:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.D);
		disassemble("CP A,D");
		break;

		case 0xBB:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.E);
		disassemble("CP A,E");
		break;

		case 0xBC:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.H);
		disassemble("CP A,H");
		break;

		case 0xBD:
		pc_cyc(1, 4);
		z80_cp8(z80.A, z80.L);
		disassemble("CP A,L");
		break;

		case 0xBE:
		pc_cyc(1, 8);
		z80_cp8(z80.A, mmu_read_mem8(z80.HL));
		disassemble("CP A,(HL)");
		break;

		case 0xFE:
		pc_cyc(2, 8);
		z80_cp8(z80.A, op2);
		disassemble("CP A,0x%02X", op2);
		break;

		case 0x20:
		pc_cyc(2, (Z_GET() == 0) ? 12 : 8);
		if ( Z_GET() == 0 )
			z80.PC += (int8_t)op2;
		disassemble("JR NZ,0x%02X", op2);
		break;

		case 0x28:
		pc_cyc(2, Z_GET() ? 12 : 8);
		if ( Z_GET() )
			z80.PC += (int8_t)op2;
		disassemble("JR Z,0x%02X", op2);
		break;

		case 0x30:
		pc_cyc(2, (C_GET() == 0) ? 12 : 8);
		if ( C_GET() == 0 )
			z80.PC += (int8_t)op2;
		disassemble("JR NC,0x%02X", op2);
		break;

		case 0x38:
		pc_cyc(2, C_GET() ? 12 : 8);
		if ( C_GET() )
			z80.PC += (int8_t)op2;
		disassemble("JR C,0x%02X", op2);
		break;

		case 0xCD:
		pc_cyc(3, 24);
		z80_call((op3 << 8) + op2);
		disassemble("CALL 0x%04X", (op3 << 8) + op2);
		break;

		case 0xC4:
		pc_cyc(3, (Z_GET() == 0) ? 24 : 12);
		if ( Z_GET() == 0 )
			z80_call((op3 << 8) + op2);
		disassemble("CALLNZ 0x%04X", (op3 << 8) + op2);
		break;

		case 0xCC:
		pc_cyc(3, Z_GET() ? 24 : 12);
		if ( Z_GET() )
			z80_call((op3 << 8) + op2);
		disassemble("CALLZ 0x%04X", (op3 << 8) + op2);
		break;

		case 0xD4:
		pc_cyc(3, (C_GET() == 0) ? 24 : 12);
		if ( C_GET() == 0 )
			z80_call((op3 << 8) + op2);
		disassemble("CALLNC 0x%04X", (op3 << 8) + op2);
		break;

		case 0xDC:
		pc_cyc(3, C_GET() ? 24 : 12);
		if ( C_GET() )
			z80_call((op3 << 8) + op2);
		disassemble("CALLC 0x%04X", (op3 << 8) + op2);
		break;

		case 0xC7:
		pc_cyc(1, 16);
		z80_call(0x00);
		disassemble("RST 0x00");
		break;

		case 0xCF:
		pc_cyc(1, 16);
		z80_call(0x08);
		disassemble("RST 0x08");
		break;

		case 0xD7:
		pc_cyc(1, 16);
		z80_call(0x10);
		disassemble("RST 0x10");
		break;

		case 0xDF:
		pc_cyc(1, 16);
		z80_call(0x18);
		disassemble("RST 0x18");
		break;

		case 0xE7:
		pc_cyc(1, 16);
		z80_call(0x20);
		disassemble("RST 0x20");
		break;

		case 0xEF:
		pc_cyc(1, 16);
		z80_call(0x28);
		disassemble("RST 0x28");
		break;

		case 0xF7:
		pc_cyc(1, 16);
		z80_call(0x30);
		disassemble("RST 0x30");
		break;

		case 0xFF:
		pc_cyc(1, 16);
		z80_call(0x38);
		disassemble("RST 0x38");
		break;

		case 0x03:
		pc_cyc(1, 8);
		z80.BC++;
		disassemble("INC BC");
		break;

		case 0x13:
		pc_cyc(1, 8);
		z80.DE++;
		disassemble("INC DE");
		break;

		case 0x23:
		pc_cyc(1, 8);
		z80.HL++;
		disassemble("INC HL");
		break;

		case 0x33:
		pc_cyc(1, 8);
		z80.SP++;
		disassemble("INC SP");
		break;

		case 0x0B:
		pc_cyc(1, 8);
		z80.BC--;
		disassemble("DEC BC");
		break;

		case 0x1B:
		pc_cyc(1, 8);
		z80.DE--;
		disassemble("DEC DE");
		break;

		case 0x2B:
		pc_cyc(1, 8);
		z80.HL--;
		disassemble("DEC HL");
		break;

		case 0x3B:
		pc_cyc(1, 8);
		z80.SP--;
		disassemble("DEC SP");
		break;

		case 0x3C:
		pc_cyc(1, 4);
		z80.A = z80_inc8(z80.A);
		disassemble("INC A");
		break;

		case 0x04:
		pc_cyc(1, 4);
		z80.B = z80_inc8(z80.B);
		disassemble("INC B");
		break;

		case 0x0C:
		pc_cyc(1, 4);
		z80.C = z80_inc8(z80.C);
		disassemble("INC C");
		break;

		case 0x14:
		pc_cyc(1, 4);
		z80.D = z80_inc8(z80.D);
		disassemble("INC D");
		break;

		case 0x1C:
		pc_cyc(1, 4);
		z80.E = z80_inc8(z80.E);
		disassemble("INC E");
		break;

		case 0x24:
		pc_cyc(1, 4);
		z80.H = z80_inc8(z80.H);
		disassemble("INC H");
		break;

		case 0x2C:
		pc_cyc(1, 4);
		z80.L = z80_inc8(z80.L);
		disassemble("INC L");
		break;

		case 0x34:
		pc_cyc(1, 12);
		mmu_write_mem8(z80.HL, z80_inc8(mmu_read_mem8(z80.HL)));
		disassemble("INC (HL)");
		break;

		case 0x3D:
		pc_cyc(1, 4);
		z80.A = z80_dec8(z80.A);
		disassemble("DEC A");
		break;

		case 0x05:
		pc_cyc(1, 4);
		z80.B = z80_dec8(z80.B);
		disassemble("DEC B");
		break;

		case 0x0D:
		pc_cyc(1, 4);
		z80.C = z80_dec8(z80.C);
		disassemble("DEC C");
		break;

		case 0x15:
		pc_cyc(1, 4);
		z80.D = z80_dec8(z80.D);
		disassemble("DEC D");
		break;

		case 0x1D:
		pc_cyc(1, 4);
		z80.E = z80_dec8(z80.E);
		disassemble("DEC E");
		break;

		case 0x25:
		pc_cyc(1, 4);
		z80.H = z80_dec8(z80.H);
		disassemble("DEC H");
		break;

		case 0x2D:
		pc_cyc(1, 4);
		z80.L = z80_dec8(z80.L);
		disassemble("DEC L");
		break;

		case 0x35:
		pc_cyc(1, 12);
		mmu_write_mem8(z80.HL, z80_dec8(mmu_read_mem8(z80.HL)));
		disassemble("DEC (HL)");
		break;

		case 0x3F:
		pc_cyc(1, 4);
		C_SET(!C_GET());
		N_SET(0);
		H_SET(0);
		disassemble("CCF");
		break;

		case 0x37:
		pc_cyc(1, 4);
		C_SET(1);
		N_SET(0);
		H_SET(0);
		disassemble("SCF");
		break;

		case 0x07:
		pc_cyc(1, 4);
		z80.A = z80_rlc8(z80.A);
		disassemble("RLCA");
		break;

		case 0x0F:
		pc_cyc(1, 4);
		z80.A = z80_rrc8(z80.A);
		disassemble("RRCA");
		break;

		case 0x17:
		pc_cyc(1, 4);
		z80.A = z80_rl8(z80.A);
		disassemble("RLA");
		break;

		case 0x1F:
		pc_cyc(1, 4);
		z80.A = z80_rr8(z80.A);
		disassemble("RRA");
		break;

		case 0x27:
		pc_cyc(1, 4);
		z80.A = z80_daa8(z80.A);
		disassemble("DAA");
		break;

		case 0x2F:
		pc_cyc(1, 4);
		z80.A = ~z80.A;
		N_SET(1);
		H_SET(1);
		disassemble("CPL");
		break;

		case 0xC9:
		pc_cyc(1, 16);
		z80.PC = z80_pop16();
		disassemble("RET");
		break;

		case 0xD9:
		pc_cyc(1, 16);
		z80.PC = z80_pop16();
		interrupt_set_ime(1);
		disassemble("RETI");
		break;

		case 0xC0:
		pc_cyc(1, (Z_GET() == 0) ? 20 : 8);
		if ( Z_GET() == 0 )
			z80.PC = z80_pop16();
		disassemble("RET NZ");
		break;

		case 0xC8:
		pc_cyc(1, Z_GET() ? 20 : 8);
		if ( Z_GET() )
			z80.PC = z80_pop16();
		disassemble("RET Z");
		break;

		case 0xD0:
		pc_cyc(1, (C_GET() == 0) ? 20 : 8);
		if ( C_GET() == 0)
			z80.PC = z80_pop16();
		disassemble("RET NC");
		break;

		case 0xD8:
		pc_cyc(1, C_GET() ? 20 : 8);
		if ( C_GET() )
			z80.PC = z80_pop16();
		disassemble("RET C");
		break;

		case 0x76:
		pc_cyc(1, 4);
		z80.halted = 1;
		disassemble("HALT");
		break;

		case 0x10:
		/* STOP instructions opcode is 10 00
		 */
		pc_cyc(2, 4);
		z80.stopped = 1;
		disassemble("STOP");
		break;

		case 0xCB:
		{
			op2 = mmu_read_mem8(z80.PC + 1);
			switch ( op2 )
			{
				case 0x37:
				pc_cyc(2, 8);
				z80.A = z80_swap8(z80.A);
				disassemble("SWAP A");
				break;

				case 0x30:
				pc_cyc(2, 8);
				z80.B = z80_swap8(z80.B);
				disassemble("SWAP B");
				break;

				case 0x31:
				pc_cyc(2, 8);
				z80.C = z80_swap8(z80.C);
				disassemble("SWAP C");
				break;

				case 0x32:
				pc_cyc(2, 8);
				z80.D = z80_swap8(z80.D);
				disassemble("SWAP D");
				break;

				case 0x33:
				pc_cyc(2, 8);
				z80.E = z80_swap8(z80.E);
				disassemble("SWAP E");
				break;

				case 0x34:
				pc_cyc(2, 8);
				z80.H = z80_swap8(z80.H);
				disassemble("SWAP H");
				break;

				case 0x35:
				pc_cyc(2, 8);
				z80.L = z80_swap8(z80.L);
				disassemble("SWAP L");
				break;

				case 0x36:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_swap8(mmu_read_mem8(z80.HL)));
				disassemble("SWAP (HL)");
				break;

				case 0x07:
				pc_cyc(2, 8);
				z80.A = z80_rlc8(z80.A);
				disassemble("RLC A");
				break;

				case 0x00:
				pc_cyc(2, 8);
				z80.B = z80_rlc8(z80.B);
				disassemble("RLC B");
				break;

				case 0x01:
				pc_cyc(2, 8);
				z80.C = z80_rlc8(z80.C);
				disassemble("RLC C");
				break;

				case 0x02:
				pc_cyc(2, 8);
				z80.D = z80_rlc8(z80.D);
				disassemble("RLC D");
				break;

				case 0x03:
				pc_cyc(2, 8);
				z80.E = z80_rlc8(z80.E);
				disassemble("RLC E");
				break;

				case 0x04:
				pc_cyc(2, 8);
				z80.H = z80_rlc8(z80.H);
				disassemble("RLC H");
				break;

				case 0x05:
				pc_cyc(2, 8);
				z80.L = z80_rlc8(z80.L);
				disassemble("RLC L");
				break;

				case 0x06:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_rlc8(mmu_read_mem8(z80.HL)));
				disassemble("RLC (HL)");
				break;

				case 0x0F:
				pc_cyc(2, 8);
				z80.A = z80_rrc8(z80.A);
				disassemble("RRC A");
				break;

				case 0x08:
				pc_cyc(2, 8);
				z80.B = z80_rrc8(z80.B);
				disassemble("RRC B");
				break;

				case 0x09:
				pc_cyc(2, 8);
				z80.C = z80_rrc8(z80.C);
				disassemble("RRC C");
				break;

				case 0x0A:
				pc_cyc(2, 8);
				z80.D = z80_rrc8(z80.D);
				disassemble("RRC D");
				break;

				case 0x0B:
				pc_cyc(2, 8);
				z80.E = z80_rrc8(z80.E);
				disassemble("RRC E");
				break;

				case 0x0C:
				pc_cyc(2, 8);
				z80.H = z80_rrc8(z80.H);
				disassemble("RRC H");
				break;

				case 0x0D:
				pc_cyc(2, 8);
				z80.L = z80_rrc8(z80.L);
				disassemble("RRC L");
				break;

				case 0x0E:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_rrc8(mmu_read_mem8(z80.HL)));
				disassemble("RRC (HL)");
				break;

				case 0x17:
				pc_cyc(2, 8);
				z80.A = z80_rl8(z80.A);
				disassemble("RL A");
				break;

				case 0x10:
				pc_cyc(2, 8);
				z80.B = z80_rl8(z80.B);
				disassemble("RL B");
				break;

				case 0x11:
				pc_cyc(2, 8);
				z80.C = z80_rl8(z80.C);
				disassemble("RL C");
				break;

				case 0x12:
				pc_cyc(2, 8);
				z80.D = z80_rl8(z80.D);
				disassemble("RL D");
				break;

				case 0x13:
				pc_cyc(2, 8);
				z80.E = z80_rl8(z80.E);
				disassemble("RL E");
				break;

				case 0x14:
				pc_cyc(2, 8);
				z80.H = z80_rl8(z80.H);
				disassemble("RL H");
				break;

				case 0x15:
				pc_cyc(2, 8);
				z80.L = z80_rl8(z80.L);
				disassemble("RL L");
				break;

				case 0x16:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_rl8(mmu_read_mem8(z80.HL)));
				disassemble("RL (HL)");
				break;

				case 0x1F:
				pc_cyc(2, 8);
				z80.A = z80_rr8(z80.A);
				disassemble("RR A");
				break;

				case 0x18:
				pc_cyc(2, 8);
				z80.B = z80_rr8(z80.B);
				disassemble("RR B");
				break;

				case 0x19:
				pc_cyc(2, 8);
				z80.C = z80_rr8(z80.C);
				disassemble("RR C");
				break;

				case 0x1A:
				pc_cyc(2, 8);
				z80.D = z80_rr8(z80.D);
				disassemble("RR D");
				break;

				case 0x1B:
				pc_cyc(2, 8);
				z80.E = z80_rr8(z80.E);
				disassemble("RR E");
				break;

				case 0x1C:
				pc_cyc(2, 8);
				z80.H = z80_rr8(z80.H);
				disassemble("RR H");
				break;

				case 0x1D:
				pc_cyc(2, 8);
				z80.L = z80_rr8(z80.L);
				disassemble("RR L");
				break;

				case 0x1E:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_rr8(mmu_read_mem8(z80.HL)));
				disassemble("RR (HL)");
				break;

				case 0x27:
				pc_cyc(2, 8);
				z80.A = z80_sla8(z80.A);
				disassemble("SLA A");
				break;

				case 0x20:
				pc_cyc(2, 8);
				z80.B = z80_sla8(z80.B);
				disassemble("SLA B");
				break;

				case 0x21:
				pc_cyc(2, 8);
				z80.C = z80_sla8(z80.C);
				disassemble("SLA C");
				break;

				case 0x22:
				pc_cyc(2, 8);
				z80.D = z80_sla8(z80.D);
				disassemble("SLA D");
				break;

				case 0x23:
				pc_cyc(2, 8);
				z80.E = z80_sla8(z80.E);
				disassemble("SLA E");
				break;

				case 0x24:
				pc_cyc(2, 8);
				z80.H = z80_sla8(z80.H);
				disassemble("SLA H");
				break;

				case 0x25:
				pc_cyc(2, 8);
				z80.L = z80_sla8(z80.L);
				disassemble("SLA L");
				break;

				case 0x26:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_sla8(mmu_read_mem8(z80.HL)));
				disassemble("SLA (HL)");
				break;

				case 0x2F:
				pc_cyc(2, 8);
				z80.A = z80_sra8(z80.A);
				disassemble("SRA A");
				break;

				case 0x28:
				pc_cyc(2, 8);
				z80.B = z80_sra8(z80.B);
				disassemble("SRA B");
				break;

				case 0x29:
				pc_cyc(2, 8);
				z80.C = z80_sra8(z80.C);
				disassemble("SRA C");
				break;

				case 0x2A:
				pc_cyc(2, 8);
				z80.D = z80_sra8(z80.D);
				disassemble("SRA D");
				break;

				case 0x2B:
				pc_cyc(2, 8);
				z80.E = z80_sra8(z80.E);
				disassemble("SRA E");
				break;

				case 0x2C:
				pc_cyc(2, 8);
				z80.H = z80_sra8(z80.H);
				disassemble("SRA H");
				break;

				case 0x2D:
				pc_cyc(2, 8);
				z80.L = z80_sra8(z80.L);
				disassemble("SRA L");
				break;

				case 0x2E:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_sra8(mmu_read_mem8(z80.HL)));
				disassemble("SRA (HL)");
				break;

				case 0x3F:
				pc_cyc(2, 8);
				z80.A = z80_srl8(z80.A);
				disassemble("SRL A");
				break;

				case 0x38:
				pc_cyc(2, 8);
				z80.B = z80_srl8(z80.B);
				disassemble("SRL B");
				break;

				case 0x39:
				pc_cyc(2, 8);
				z80.C = z80_srl8(z80.C);
				disassemble("SRL C");
				break;

				case 0x3A:
				pc_cyc(2, 8);
				z80.D = z80_srl8(z80.D);
				disassemble("SRL D");
				break;

				case 0x3B:
				pc_cyc(2, 8);
				z80.E = z80_srl8(z80.E);
				disassemble("SRL E");
				break;

				case 0x3C:
				pc_cyc(2, 8);
				z80.H = z80_srl8(z80.H);
				disassemble("SRL H");
				break;

				case 0x3D:
				pc_cyc(2, 8);
				z80.L = z80_srl8(z80.L);
				disassemble("SRL L");
				break;

				case 0x3E:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_srl8(mmu_read_mem8(z80.HL)));
				disassemble("SRL (HL)");
				break;

				case 0x40:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 0);
				disassemble("BIT B,0");
				break;

				case 0x41:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 0);
				disassemble("BIT C,0");
				break;

				case 0x42:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 0);
				disassemble("BIT D,0");
				break;

				case 0x43:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 0);
				disassemble("BIT E,0");
				break;

				case 0x44:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 0);
				disassemble("BIT H,0");
				break;

				case 0x45:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 0);
				disassemble("BIT L,0");
				break;

				case 0x46:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 0);
				disassemble("BIT (HL),0");
				break;

				case 0x47:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 0);
				disassemble("BIT A,0");
				break;

				case 0x48:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 1);
				disassemble("BIT B,1");
				break;

				case 0x49:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 1);
				disassemble("BIT C,1");
				break;

				case 0x4A:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 1);
				disassemble("BIT D,1");
				break;

				case 0x4B:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 1);
				disassemble("BIT E,1");
				break;

				case 0x4C:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 1);
				disassemble("BIT H,1");
				break;

				case 0x4D:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 1);
				disassemble("BIT L,1");
				break;

				case 0x4E:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 1);
				disassemble("BIT (HL),1");
				break;

				case 0x4F:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 1);
				disassemble("BIT A,1");
				break;

				case 0x50:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 2);
				disassemble("BIT B,2");
				break;

				case 0x51:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 2);
				disassemble("BIT C,2");
				break;

				case 0x52:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 2);
				disassemble("BIT D,2");
				break;

				case 0x53:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 2);
				disassemble("BIT E,2");
				break;

				case 0x54:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 2);
				disassemble("BIT H,2");
				break;

				case 0x55:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 2);
				disassemble("BIT L,2");
				break;

				case 0x56:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 2);
				disassemble("BIT (HL),2");
				break;

				case 0x57:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 2);
				disassemble("BIT A,2");
				break;

				case 0x58:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 3);
				disassemble("BIT B,3");
				break;

				case 0x59:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 3);
				disassemble("BIT C,3");
				break;

				case 0x5A:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 3);
				disassemble("BIT D,3");
				break;

				case 0x5B:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 3);
				disassemble("BIT E,3");
				break;

				case 0x5C:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 3);
				disassemble("BIT H,3");
				break;

				case 0x5D:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 3);
				disassemble("BIT L,3");
				break;

				case 0x5E:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 3);
				disassemble("BIT (HL),3");
				break;

				case 0x5F:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 3);
				disassemble("BIT A,3");
				break;

				case 0x60:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 4);
				disassemble("BIT B,4");
				break;

				case 0x61:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 4);
				disassemble("BIT C,4");
				break;

				case 0x62:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 4);
				disassemble("BIT D,4");
				break;

				case 0x63:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 4);
				disassemble("BIT E,4");
				break;

				case 0x64:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 4);
				disassemble("BIT H,4");
				break;

				case 0x65:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 4);
				disassemble("BIT L,4");
				break;

				case 0x66:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 4);
				disassemble("BIT (HL),4");
				break;

				case 0x67:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 4);
				disassemble("BIT A,4");
				break;

				case 0x68:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 5);
				disassemble("BIT B,5");
				break;

				case 0x69:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 5);
				disassemble("BIT C,5");
				break;

				case 0x6A:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 5);
				disassemble("BIT D,5");
				break;

				case 0x6B:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 5);
				disassemble("BIT E,5");
				break;

				case 0x6C:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 5);
				disassemble("BIT H,5");
				break;

				case 0x6D:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 5);
				disassemble("BIT L,5");
				break;

				case 0x6E:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 5);
				disassemble("BIT (HL),5");
				break;

				case 0x6F:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 5);
				disassemble("BIT A,5");
				break;

				case 0x70:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 6);
				disassemble("BIT B,6");
				break;

				case 0x71:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 6);
				disassemble("BIT C,6");
				break;

				case 0x72:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 6);
				disassemble("BIT D,6");
				break;

				case 0x73:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 6);
				disassemble("BIT E,6");
				break;

				case 0x74:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 6);
				disassemble("BIT H,6");
				break;

				case 0x75:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 6);
				disassemble("BIT L,6");
				break;

				case 0x76:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 6);
				disassemble("BIT (HL),6");
				break;

				case 0x77:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 6);
				disassemble("BIT A,6");
				break;

				case 0x78:
				pc_cyc(2, 8);
				z80_bit8(z80.B, 7);
				disassemble("BIT B,7");
				break;

				case 0x79:
				pc_cyc(2, 8);
				z80_bit8(z80.C, 7);
				disassemble("BIT C,7");
				break;

				case 0x7A:
				pc_cyc(2, 8);
				z80_bit8(z80.D, 7);
				disassemble("BIT D,7");
				break;

				case 0x7B:
				pc_cyc(2, 8);
				z80_bit8(z80.E, 7);
				disassemble("BIT E,7");
				break;

				case 0x7C:
				pc_cyc(2, 8);
				z80_bit8(z80.H, 7);
				disassemble("BIT H,7");
				break;

				case 0x7D:
				pc_cyc(2, 8);
				z80_bit8(z80.L, 7);
				disassemble("BIT L,7");
				break;

				case 0x7E:
				pc_cyc(2, 12);
				z80_bit8(mmu_read_mem8(z80.HL), 7);
				disassemble("BIT (HL),7");
				break;

				case 0x7F:
				pc_cyc(2, 8);
				z80_bit8(z80.A, 7);
				disassemble("BIT A,7");
				break;

				case 0xC0:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 0);
				disassemble("SET 0,B");
				break;

				case 0xC1:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 0);
				disassemble("SET 0,C");
				break;

				case 0xC2:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 0);
				disassemble("SET 0,D");
				break;

				case 0xC3:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 0);
				disassemble("SET 0,E");
				break;

				case 0xC4:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 0);
				disassemble("SET 0,H");
				break;

				case 0xC5:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 0);
				disassemble("SET 0,L");
				break;

				case 0xC6:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 0));
				disassemble("SET 0,(HL)");
				break;

				case 0xC7:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 0);
				disassemble("SET 0,A");
				break;

				case 0xC8:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 1);
				disassemble("SET 1,B");
				break;

				case 0xC9:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 1);
				disassemble("SET 1,C");
				break;

				case 0xCA:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 1);
				disassemble("SET 1,D");
				break;

				case 0xCB:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 1);
				disassemble("SET 1,E");
				break;

				case 0xCC:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 1);
				disassemble("SET 1,H");
				break;

				case 0xCD:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 1);
				disassemble("SET 1,L");
				break;

				case 0xCE:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 1));
				disassemble("SET 1,(HL)");
				break;

				case 0xCF:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 1);
				disassemble("SET 1,A");
				break;

				case 0xD0:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 2);
				disassemble("SET 2,B");
				break;

				case 0xD1:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 2);
				disassemble("SET 2,C");
				break;

				case 0xD2:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 2);
				disassemble("SET 2,D");
				break;

				case 0xD3:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 2);
				disassemble("SET 2,E");
				break;

				case 0xD4:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 2);
				disassemble("SET 2,H");
				break;

				case 0xD5:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 2);
				disassemble("SET 2,L");
				break;

				case 0xD6:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 2));
				disassemble("SET 2,(HL)");
				break;

				case 0xD7:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 2);
				disassemble("SET 2,A");
				break;

				case 0xD8:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 3);
				disassemble("SET 3,B");
				break;

				case 0xD9:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 3);
				disassemble("SET 3,C");
				break;

				case 0xDA:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 3);
				disassemble("SET 3,D");
				break;

				case 0xDB:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 3);
				disassemble("SET 3,E");
				break;

				case 0xDC:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 3);
				disassemble("SET 3,H");
				break;

				case 0xDD:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 3);
				disassemble("SET 3,L");
				break;

				case 0xDE:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 3));
				disassemble("SET 3,(HL)");
				break;

				case 0xDF:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 3);
				disassemble("SET 3,A");
				break;

				case 0xE0:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 4);
				disassemble("SET 4,B");
				break;

				case 0xE1:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 4);
				disassemble("SET 4,C");
				break;

				case 0xE2:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 4);
				disassemble("SET 4,D");
				break;

				case 0xE3:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 4);
				disassemble("SET 4,E");
				break;

				case 0xE4:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 4);
				disassemble("SET 4,H");
				break;

				case 0xE5:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 4);
				disassemble("SET 4,L");
				break;

				case 0xE6:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 4));
				disassemble("SET 4,(HL)");
				break;

				case 0xE7:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 4);
				disassemble("SET 4,A");
				break;

				case 0xE8:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 5);
				disassemble("SET 5,B");
				break;

				case 0xE9:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 5);
				disassemble("SET 5,C");
				break;

				case 0xEA:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 5);
				disassemble("SET 5,D");
				break;

				case 0xEB:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 5);
				disassemble("SET 5,E");
				break;

				case 0xEC:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 5);
				disassemble("SET 5,H");
				break;

				case 0xED:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 5);
				disassemble("SET 5,L");
				break;

				case 0xEE:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 5));
				disassemble("SET 5,(HL)");
				break;

				case 0xEF:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 5);
				disassemble("SET 5,A");
				break;

				case 0xF0:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 6);
				disassemble("SET 6,B");
				break;

				case 0xF1:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 6);
				disassemble("SET 6,C");
				break;

				case 0xF2:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 6);
				disassemble("SET 6,D");
				break;

				case 0xF3:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 6);
				disassemble("SET 6,E");
				break;

				case 0xF4:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 6);
				disassemble("SET 6,H");
				break;

				case 0xF5:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 6);
				disassemble("SET 6,L");
				break;

				case 0xF6:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 6));
				disassemble("SET 6,(HL)");
				break;

				case 0xF7:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 6);
				disassemble("SET 6,A");
				break;

				case 0xF8:
				pc_cyc(2, 8);
				z80.B = z80_set8(z80.B, 7);
				disassemble("SET 7,B");
				break;

				case 0xF9:
				pc_cyc(2, 8);
				z80.C = z80_set8(z80.C, 7);
				disassemble("SET 7,C");
				break;

				case 0xFA:
				pc_cyc(2, 8);
				z80.D = z80_set8(z80.D, 7);
				disassemble("SET 7,D");
				break;

				case 0xFB:
				pc_cyc(2, 8);
				z80.E = z80_set8(z80.E, 7);
				disassemble("SET 7,E");
				break;

				case 0xFC:
				pc_cyc(2, 8);
				z80.H = z80_set8(z80.H, 7);
				disassemble("SET 7,H");
				break;

				case 0xFD:
				pc_cyc(2, 8);
				z80.L = z80_set8(z80.L, 7);
				disassemble("SET 7,L");
				break;

				case 0xFE:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_set8(mmu_read_mem8(z80.HL), 7));
				disassemble("SET 7,(HL)");
				break;

				case 0xFF:
				pc_cyc(2, 8);
				z80.A = z80_set8(z80.A, 7);
				disassemble("SET 7,A");
				break;

				case 0x80:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 0);
				disassemble("RES B,0");
				break;

				case 0x81:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 0);
				disassemble("RES C,0");
				break;

				case 0x82:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 0);
				disassemble("RES D,0");
				break;

				case 0x83:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 0);
				disassemble("RES E,0");
				break;

				case 0x84:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 0);
				disassemble("RES H,0");
				break;

				case 0x85:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 0);
				disassemble("RES L,0");
				break;

				case 0x86:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 0));
				disassemble("RES (HL),0");
				break;

				case 0x87:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 0);
				disassemble("RES A,0");
				break;

				case 0x88:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 1);
				disassemble("RES B,1");
				break;

				case 0x89:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 1);
				disassemble("RES C,1");
				break;

				case 0x8A:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 1);
				disassemble("RES D,1");
				break;

				case 0x8B:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 1);
				disassemble("RES E,1");
				break;

				case 0x8C:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 1);
				disassemble("RES H,1");
				break;

				case 0x8D:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 1);
				disassemble("RES L,1");
				break;

				case 0x8E:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 1));
				disassemble("RES (HL),1");
				break;

				case 0x8F:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 1);
				disassemble("RES A,1");
				break;

				case 0x90:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 2);
				disassemble("RES B,2");
				break;

				case 0x91:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 2);
				disassemble("RES C,2");
				break;

				case 0x92:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 2);
				disassemble("RES D,2");
				break;

				case 0x93:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 2);
				disassemble("RES E,2");
				break;

				case 0x94:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 2);
				disassemble("RES H,2");
				break;

				case 0x95:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 2);
				disassemble("RES L,2");
				break;

				case 0x96:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 2));
				disassemble("RES (HL),2");
				break;

				case 0x97:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 2);
				disassemble("RES A,2");
				break;

				case 0x98:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 3);
				disassemble("RES B,3");
				break;

				case 0x99:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 3);
				disassemble("RES C,3");
				break;

				case 0x9A:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 3);
				disassemble("RES D,3");
				break;

				case 0x9B:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 3);
				disassemble("RES E,3");
				break;

				case 0x9C:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 3);
				disassemble("RES H,3");
				break;

				case 0x9D:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 3);
				disassemble("RES L,3");
				break;

				case 0x9E:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 3));
				disassemble("RES (HL),3");
				break;

				case 0x9F:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 3);
				disassemble("RES A,3");
				break;

				case 0xA0:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 4);
				disassemble("RES B,4");
				break;

				case 0xA1:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 4);
				disassemble("RES C,4");
				break;

				case 0xA2:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 4);
				disassemble("RES D,4");
				break;

				case 0xA3:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 4);
				disassemble("RES E,4");
				break;

				case 0xA4:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 4);
				disassemble("RES H,4");
				break;

				case 0xA5:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 4);
				disassemble("RES L,4");
				break;

				case 0xA6:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 4));
				disassemble("RES (HL),4");
				break;

				case 0xA7:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 4);
				disassemble("RES A,4");
				break;

				case 0xA8:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 5);
				disassemble("RES B,5");
				break;

				case 0xA9:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 5);
				disassemble("RES C,5");
				break;

				case 0xAA:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 5);
				disassemble("RES D,5");
				break;

				case 0xAB:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 5);
				disassemble("RES E,5");
				break;

				case 0xAC:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 5);
				disassemble("RES H,5");
				break;

				case 0xAD:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 5);
				disassemble("RES L,5");
				break;

				case 0xAE:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 5));
				disassemble("RES (HL),5");
				break;

				case 0xAF:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 5);
				disassemble("RES A,5");
				break;

				case 0xB0:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 6);
				disassemble("RES B,6");
				break;

				case 0xB1:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 6);
				disassemble("RES C,6");
				break;

				case 0xB2:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 6);
				disassemble("RES D,6");
				break;

				case 0xB3:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 6);
				disassemble("RES E,6");
				break;

				case 0xB4:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 6);
				disassemble("RES H,6");
				break;

				case 0xB5:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 6);
				disassemble("RES L,6");
				break;

				case 0xB6:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 6));
				disassemble("RES (HL),6");
				break;

				case 0xB7:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 6);
				disassemble("RES A,6");
				break;

				case 0xB8:
				pc_cyc(2, 8);
				z80.B = z80_res8(z80.B, 7);
				disassemble("RES B,7");
				break;

				case 0xB9:
				pc_cyc(2, 8);
				z80.C = z80_res8(z80.C, 7);
				disassemble("RES C,7");
				break;

				case 0xBA:
				pc_cyc(2, 8);
				z80.D = z80_res8(z80.D, 7);
				disassemble("RES D,7");
				break;

				case 0xBB:
				pc_cyc(2, 8);
				z80.E = z80_res8(z80.E, 7);
				disassemble("RES E,7");
				break;

				case 0xBC:
				pc_cyc(2, 8);
				z80.H = z80_res8(z80.H, 7);
				disassemble("RES H,7");
				break;

				case 0xBD:
				pc_cyc(2, 8);
				z80.L = z80_res8(z80.L, 7);
				disassemble("RES L,7");
				break;

				case 0xBE:
				pc_cyc(2, 16);
				mmu_write_mem8(z80.HL, z80_res8(mmu_read_mem8(z80.HL), 7));
				disassemble("RES (HL),7");
				break;

				case 0xBF:
				pc_cyc(2, 8);
				z80.A = z80_res8(z80.A, 7);
				disassemble("RES A,7");
				break;

				default:
				goto unknown_opcode;
			}
		}
		break;

		unknown_opcode:
		default:
		fprintf(stderr, "FATAL: unknown opcode 0x%02X at 0x%04X\n", op1, z80.PC);
		exit(1);
	}

	if ( z80.enable_interrupt )
		interrupt_set_ime(1);
	if ( z80.disable_interrupt )
		interrupt_set_ime(0);

	z80.enable_interrupt = enable_interrupt;
	z80.disable_interrupt = disable_interrupt;

	return cycles;
}

int32_t z80_dump(FILE *file)
{
	if ( fwrite(&z80, 1, sizeof(z80), file) != sizeof(z80) )
		return -1;
	return 0;
}

int32_t z80_restore(FILE *file)
{
	if ( fread(&z80, 1, sizeof(z80), file) != sizeof(z80) )
		return -1;
	return 0;
}
