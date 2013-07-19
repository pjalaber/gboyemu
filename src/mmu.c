#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "rom.h"
#include "mmu.h"
#include "gpu.h"
#include "joypad.h"
#include "interrupt.h"
#include "serial.h"
#include "divider.h"
#include "timer.h"
#include "sound.h"

static uint8_t bios[256] =
{
	/* 0000 */   0x31, 0xFE, 0xFF,         /* LD SP,0xFFFE     */
	/* 0003 */   0xAF,                     /* XOR A,A          */
	/* 0004 */   0x21, 0xFF, 0x9F,         /* LD HL,0x9FFF     */
	/* 0007 */   0x32,                     /* LDD (HL),A       */
	/* 0008 */   0xCB, 0x7C,               /* BIT H,7          */
	/* 000A */   0x20, 0xFB,               /* JR NZ,0xFB       */
	/* 000C */   0x21, 0x26, 0xFF,         /* LD HL,0xFF26     */
	/* 000F */   0x0E, 0x11,               /* LD C,0x11        */
	/* 0011 */   0x3E, 0x80,               /* LD A,0x80        */
	/* 0013 */   0x32,                     /* LDD (HL),A       */
	/* 0014 */   0xE2,                     /* LD (0xFF00+C),A  */
	/* 0015 */   0x0C,                     /* INC C            */
	/* 0016 */   0x3E, 0xF3,               /* LD A,0xF3        */
	/* 0018 */   0xE2,                     /* LD (0xFF00+C),A  */
	/* 0019 */   0x32,                     /* LDD (HL),A       */
	/* 001A */   0x3E, 0x77,               /* LD A,0x77        */
	/* 001C */   0x77,                     /* LD (HL),A        */
	/* 001D */   0x3E, 0xFC,               /* LD A,0xFC        */
	/* 001F */   0xE0, 0x47,               /* LD (0xFF47),A    */
	/* 0021 */   0x11, 0x04, 0x01,         /* LD DE,0x0104     */
	/* 0024 */   0x21, 0x10, 0x80,         /* LD HL,0x8010     */
	/* 0027 */   0x1A,                     /* LD A,(DE)        */
	/* 0028 */   0xCD, 0x95, 0x00,         /* CALL 0x0095      */
	/* 002B */   0xCD, 0x96, 0x00,         /* CALL 0x0096      */
	/* 002E */   0x13,                     /* INC DE           */
	/* 002F */   0x7B,                     /* LD A,E           */
	/* 0030 */   0xFE, 0x34,               /* CP A,0x34        */
	/* 0032 */   0x20, 0xF3,               /* JR NZ,0xF3       */
	/* 0034 */   0x11, 0xD8, 0x00,         /* LD DE,0x00D8     */
	/* 0037 */   0x06, 0x08,               /* LD B,0x08        */
	/* 0039 */   0x1A,                     /* LD A,(DE)        */
	/* 003A */   0x13,                     /* INC DE           */
	/* 003B */   0x22,                     /* LDI (HL),A       */
	/* 003C */   0x23,                     /* INC HL           */
	/* 003D */   0x05,                     /* DEC B            */
	/* 003E */   0x20, 0xF9,               /* JR NZ,0xF9       */
	/* 0040 */   0x3E, 0x19,               /* LD A,0x19        */
	/* 0042 */   0xEA, 0x10, 0x99,         /* LD (0x9910),A    */
	/* 0045 */   0x21, 0x2F, 0x99,         /* LD HL,0x992F     */
	/* 0048 */   0x0E, 0x0C,               /* LD C,0x0C        */
	/* 004A */   0x3D,                     /* DEC A            */
	/* 004B */   0x28, 0x08,               /* JR Z,0x08        */
	/* 004D */   0x32,                     /* LDD (HL),A       */
	/* 004E */   0x0D,                     /* DEC C            */
	/* 004F */   0x20, 0xF9,               /* JR NZ,0xF9       */
	/* 0051 */   0x2E, 0x0F,               /* LD L,0x0F        */
	/* 0053 */   0x18, 0xF3,               /* JR 0xF3          */
	/* 0055 */   0x67,                     /* LD H,A           */
	/* 0056 */   0x3E, 0x64,               /* LD A,0x64        */
	/* 0058 */   0x57,                     /* LD D,A           */
	/* 0059 */   0xE0, 0x42,               /* LD (0xFF42),A    */
	/* 005B */   0x3E, 0x91,               /* LD A,0x91        */
	/* 005D */   0xE0, 0x40,               /* LD (0xFF40),A    */
	/* 005F */   0x04,                     /* INC B            */
	/* 0060 */   0x1E, 0x02,               /* LD E,0x02        */
	/* 0062 */   0x0E, 0x0C,               /* LD C,0x0C        */
	/* 0064 */   0xF0, 0x44,               /* LD A,(0xFF44)    */
	/* 0066 */   0xFE, 0x90,               /* CP A,0x90        */
	/* 0068 */   0x20, 0xFA,               /* JR NZ,0xFA       */
	/* 006A */   0x0D,                     /* DEC C            */
	/* 006B */   0x20, 0xF7,               /* JR NZ,0xF7       */
	/* 006D */   0x1D,                     /* DEC E            */
	/* 006E */   0x20, 0xF2,               /* JR NZ,0xF2       */
	/* 0070 */   0x0E, 0x13,               /* LD C,0x13        */
	/* 0072 */   0x24,                     /* INC H            */
	/* 0073 */   0x7C,                     /* LD A,H           */
	/* 0074 */   0x1E, 0x83,               /* LD E,0x83        */
	/* 0076 */   0xFE, 0x62,               /* CP A,0x62        */
	/* 0078 */   0x28, 0x06,               /* JR Z,0x06        */
	/* 007A */   0x1E, 0xC1,               /* LD E,0xC1        */
	/* 007C */   0xFE, 0x64,               /* CP A,0x64        */
	/* 007E */   0x20, 0x06,               /* JR NZ,0x06       */
	/* 0080 */   0x7B,                     /* LD A,E           */
	/* 0081 */   0xE2,                     /* LD (0xFF00+C),A  */
	/* 0082 */   0x0C,                     /* INC C            */
	/* 0083 */   0x3E, 0x87,               /* LD A,0x87        */
	/* 0085 */   0xF2,                     /* LD A,(0xFF00+C)  */
	/* 0086 */   0xF0, 0x42,               /* LD A,(0xFF42)    */
	/* 0088 */   0x90,                     /* SUB A,B          */
	/* 0089 */   0xE0, 0x42,               /* LD (0xFF42),A    */
	/* 008B */   0x15,                     /* DEC D            */
	/* 008C */   0x20, 0xD2,               /* JR NZ,0xD2       */
	/* 008E */   0x05,                     /* DEC B            */
	/* 008F */   0x20, 0x4F,               /* JR NZ,0x4F       */
	/* 0091 */   0x16, 0x20,               /* LD D,0x20        */
	/* 0093 */   0x18, 0xCB,               /* JR 0xCB          */
	/* 0095 */   0x4F,                     /* LD C,A           */
	/* 0096 */   0x06, 0x04,               /* LD B,0x04        */
	/* 0098 */   0xC5,                     /* PUSH BC          */
	/* 0099 */   0xCB, 0x11,               /* RL C             */
	/* 009B */   0x17,                     /* RLA              */
	/* 009C */   0xC1,                     /* POP BC           */
	/* 009D */   0xCB, 0x11,               /* RL C             */
	/* 009F */   0x17,                     /* RLA              */
	/* 00A0 */   0x05,                     /* DEC B            */
	/* 00A1 */   0x20, 0xF5,               /* JR NZ,0xF5       */
	/* 00A3 */   0x22,                     /* LDI (HL),A       */
	/* 00A4 */   0x23,                     /* INC HL           */
	/* 00A5 */   0x22,                     /* LDI (HL),A       */
	/* 00A6 */   0x23,                     /* INC HL           */
	/* 00A7 */   0xC9,                     /* RET              */
	/* 00A8 */   0xCE, 0xED,               /* ADC A,0xED       */
	/* 00AA */   0x66,                     /* LD H,(HL)        */
	/* 00AB */   0x66,                     /* LD H,(HL)        */
	/* 00AC */   0xCC, 0x0D, 0x00,         /* CALLZ 0x000D     */
	/* 00AF */   0x0B,                     /* DEC BC           */
	/* 00B0 */   0x03,                     /* INC BC           */
	/* 00B1 */   0x73,                     /* LD (HL),E        */
	/* 00B2 */   0x00,                     /* NOP              */
	/* 00B3 */   0x83,                     /* ADD A,E          */
	/* 00B4 */   0x00,                     /* NOP              */
	/* 00B5 */   0x0C,                     /* INC C            */
	/* 00B6 */   0x00,                     /* NOP              */
	/* 00B7 */   0x0D,                     /* DEC C            */
	/* 00B8 */   0x00,                     /* NOP              */
	/* 00B9 */   0x08, 0x11, 0x1F,         /* LD (0x1F11),SP   */
	/* 00BC */   0x88,                     /* ADC A,B          */
	/* 00BD */   0x89,                     /* ADC A,C          */
	/* 00BE */   0x00,                     /* NOP              */
	/* 00BF */   0x0E, 0xDC,               /* LD C,0xDC        */

	0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
	0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E, 0x3c, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x4C,
	0x21, 0x04, 0x01, 0x11, 0xA8, 0x00, 0x1A, 0x13, 0xBE, 0x20, 0xFE, 0x23, 0x7D, 0xFE, 0x34, 0x20,
	0xF5, 0x06, 0x19, 0x78, 0x86, 0x23, 0x05, 0x20, 0xFB, 0x86, 0x20, 0xFE, 0x3E, 0x01, 0xE0, 0x50
};

static uint32_t run_bios;
static struct
{
	uint8_t work_ram[0x2000];
	uint8_t io[0x80];
	uint8_t high_ram[0x7F];
} mem;

int32_t mmu_init(void)
{
	memset(&mem, 0, sizeof(mem));
	run_bios = 0;
	return 0;
}

uint8_t mmu_read_mem8(uint16_t addr)
{
	if ( run_bios  )
	{
		if ( addr < 0x100 )
			return bios[addr];
		else if ( addr == 0x100 )
		{
			fprintf(stderr, "Exiting BIOS 0x%04x\n", addr);
			run_bios = 0;
		}
	}
	if ( addr <= 0x7FFF )
	{
		return rom_read_rom8(addr);
	}
	else if ( addr <= 0x9FFF )
	{
		return gpu_read_vram(addr - 0x8000);
	}
	else if ( addr <= 0xBFFF )
	{
		return rom_read_ram8(addr - 0xA000);
	}
	else if ( addr <= 0xDFFF )
	{
		return mem.work_ram[addr - 0xC000];
	}
	else if ( addr <= 0xFDFF )
	{
		return mem.work_ram[addr - 0xE000];
	}
	else if ( addr <= 0xFE9F )
	{
		return gpu_read_oam(addr - 0xFE00);
	}
	else if ( addr <= 0xFEFF )
	{
		/* memory area is not usable
		 */
		assert(0);
		return 0;
	}
	else if ( addr <= 0xFF7F )
	{
		switch (addr)
		{
			/* P1 - Joypad
			 */
			case 0xFF00:
			return joypad_get();
			break;

			/* SB - Serial transfer data
			 */
			case 0xFF01:
			return serial_read_data();
			break;

			/* SC - SIO Control
			 */
			case 0xFF02:
			return serial_read_ctrl();
			break;

			/* DIV - Divider Register
			 */
			case 0xFF04:
			return divider_get_counter();
			break;

			/* TIMA - Timer counter
			 */
			case 0xFF05:
			return timer_get_counter();
			break;

			/* TMA - Timer Modulo
			 */
			case 0xFF06:
			return timer_get_modulo();
			break;

			/* TAC - Timer Control
			 */
			case 0xFF07:
			return timer_get_control();
			break;

			/* IF - Interrupt Flag
			 */
			case 0xFF0F:
			return interrupt_get_flag();
			break;

			/* NR10 - Sound Mode 1 register
			 */
			case 0xFF10:
			return sound_read_NR10();
			break;

			/* NR11 - Sound Mode 1 register,
			 * Sound length/wave pattern Duty
			 */
			case 0xFF11:
			return sound_read_NR11();
			break;

			/* NR12 - Sound Mode 1 register,
			 * Envelope
			 */
			case 0xFF12:
			return sound_read_NR12();
			break;

			/* NR13 - Sound Mode 1 register,
			 * Frequency lo data
			 */
			case 0xFF13:
			return sound_read_NR13();
			break;

			/* NR14 - Sound Mode 1 register,
			 * Frequency hi data
			 */
			case 0xFF14:
			return sound_read_NR14();
			break;

			/* NR21 - Sound Mode 2 register,
			 * Sound length/wave pattern Duty
			 */
			case 0xFF16:
			return sound_read_NR21();
			break;

			/* NR22 - Sound Mode 2 register,
			 * Envelope
			 */
			case 0xFF17:
			return sound_read_NR22();
			break;

			/* NR23 - Sound Mode 2 register,
			 * Frequency lo data
			 */
			case 0xFF18:
			return sound_read_NR23();
			break;

			/* NR24 - Sound Mode 2 register,
			 * Frequency hi data
			 */
			case 0xFF19:
			return sound_read_NR24();
			break;

			/* NR30 - Sound Mode 3 register,
			 * Sound on/off
			 */
			case 0xFF1A:
			return sound_read_NR30();
			break;

			/* NR31 - Sound Mode 3 register,
			 * Sound length
			 */
			case 0xFF1B:
			return sound_read_NR31();
			break;

			/* NR32 - Sound Mode 3 register,
			 * Select output level
			 */
			case 0xFF1C:
			return sound_read_NR32();
			break;

			/* NR33 - Sound Mode 3 register,
			 * frequency's lower data
			 */
			case 0xFF1D:
			return sound_read_NR33();
			break;

			/* NR34 - Sound Mode 3 register,
			 * frequency's higher data
			 */
			case 0xFF1E:
			return sound_read_NR34();
			break;

			/* NR41 - Sound Mode 4 register,
			 * Sound length
			 */
			case 0xFF20:
			return sound_read_NR41();
			break;

			/* NR42 - Sound Mode 4 register,
			 * Envelope
			 */
			case 0xFF21:
			return sound_read_NR42();
			break;

			/* NR43 - Sound Mode 4 register,
			 * Polynomial counter
			 */
			case 0xFF22:
			return sound_read_NR43();
			break;

			/* NR44 - Sound Mode 4 register,
			 * Counter/consecutive; initial
			 */
			case 0xFF23:
			return sound_read_NR44();
			break;

			/* NR50 - Channel control / ON-OFF / Volume
			 */
			case 0xFF24:
			return sound_read_NR50();
			break;

			/* NR51 - Selection of Sound output terminal
			 */
			case 0xFF25:
			return sound_read_NR51();
			break;

			/* NR52 - Contents - Sound on/off
			 */
			case 0xFF26:
			return sound_read_NR52();
			break;

			/* Wave Pattern RAM
			 */
			case 0xFF30:
			case 0xFF31:
			case 0xFF32:
			case 0xFF33:
			case 0xFF34:
			case 0xFF35:
			case 0xFF36:
			case 0xFF37:
			case 0xFF38:
			case 0xFF39:
			case 0xFF3A:
			case 0xFF3B:
			case 0xFF3C:
			case 0xFF3D:
			case 0xFF3E:
			case 0xFF3F:
			return sound_read_wavepattern(addr - 0xFF30);
			break;

			/* LCDC - LCD Control
			 */
			case 0xFF40:
			return gpu_read_lcdctrl();
			break;

			/* STAT - LCD Status
			 */
			case 0xFF41:
			return gpu_read_lcdstatus();
			break;

			/* SCY - Scroll Y
			 */
			case 0xFF42:
			return gpu_read_scrolly();
			break;

			/* SCX - Scroll X
			 */
			case 0xFF43:
			return gpu_read_scrollx();
			break;

			/* LY - LCDC Y-Coordinate
			 */
			case 0xFF44:
			return gpu_read_ly();
			break;

			/* LYC - LY Compare
			 */
			case 0xFF45:
			return gpu_read_lycmp();
			break;

			/* DMA - DMA Transfer and Start Address
			 */
			case 0xFF46:
			/* write-only register
			 */
			return 0;
			break;

			/* BGP - Background and Window Palette Data
			 */
			case 0xFF47:
			return gpu_read_bgp();
			break;

			/* OBP0 - Object Palette 0 Data
			 */
			case 0xFF48:
			return gpu_read_objpal0();
			break;

			/* OBP1 - Object Palette 1 Data
			 */
			case 0xFF49:
			return gpu_read_objpal1();
			break;

			/* WY - Window Y Position
			 */
			case 0xFF4A:
			return gpu_read_windowy();
			break;

			/* WX - Window X Position
			 */
			case 0xFF4B:
			return gpu_read_windowx();
			break;

			default:
			return mem.io[addr - 0xFF00];
		}
	}
	else if ( addr <= 0xFFFE )
	{
		return mem.high_ram[addr - 0xFF80];
	}
	else
	{
		/* IE - Interrupt Enable
		 */
		return interrupt_get_ie();
	}
}

uint16_t mmu_read_mem16(uint16_t addr)
{
	return mmu_read_mem8(addr) | (mmu_read_mem8(addr+1) << 8);
}


void mmu_write_mem8(uint16_t addr, uint8_t value8)
{
	if ( addr <= 0x7FFF )
	{
		rom_write_rom8(addr, value8);
		return;
	}
	else if ( addr <= 0x9FFF )
	{
		gpu_write_vram(addr - 0x8000, value8);
		return;
	}
	else if ( addr <= 0xBFFF )
	{
		rom_write_ram8(addr - 0xA000, value8);
		return;
	}
	else if ( addr <= 0xDFFF )
	{
		mem.work_ram[addr - 0xC000] = value8;
		return;
	}
	else if ( addr <= 0xFDFF )
	{
		mem.work_ram[addr - 0xE000] = value8;
		return;
	}
	else if ( addr <= 0xFE9F )
	{
		gpu_write_oam(addr - 0xFE00, value8);
		return;
	}
	else if ( addr <= 0xFEFF )
	{
		/* memory area is not usable
		 */
		return;
	}
	else if ( addr <= 0xFF7F )
	{
		switch (addr)
		{
			/* P1 - Joypad
			 */
			case 0xFF00:
			joypad_set(value8);
			return;

			/* SB - Serial transfer data
			 */
			case 0xFF01:
			serial_write_data(value8);
			return;

			/* SC - SIO Control
			 */
			case 0xFF02:
			serial_write_ctrl(value8);
			return;

			/* DIV - Divider Register
			 */
			case 0xFF04:
			divider_set_counter(value8);
			return;

			/* TIMA - Timer counter
			 */
			case 0xFF05:
			timer_set_counter(value8);
			return;

			/* TMA - Timer Modulo
			 */
			case 0xFF06:
			timer_set_modulo(value8);
			return;

			/* TAC - Timer Control
			 */
			case 0xFF07:
			timer_set_control(value8);
			return;

			/* IF - Interrupt Flag
			 */
			case 0xFF0F:
			interrupt_set_flag(value8);
			return;

			/* NR10 - Sound Mode 1 register
			 */
			case 0xFF10:
			sound_write_NR10(value8);
			return;

			/* NR11 - Sound Mode 1 register,
			 * Sound length/wave pattern Duty
			 */
			case 0xFF11:
			sound_write_NR11(value8);
			return;

			/* NR12 - Sound Mode 1 register,
			 * Envelope
			 */
			case 0xFF12:
			sound_write_NR12(value8);
			return;

			/* NR13 - Sound Mode 1 register,
			 * Frequency lo data
			 */
			case 0xFF13:
			sound_write_NR13(value8);
			return;

			/* NR14 - Sound Mode 1 register,
			 * Frequency hi data
			 */
			case 0xFF14:
			sound_write_NR14(value8);
			return;

			/* NR21 - Sound Mode 2 register,
			 * Sound length/wave pattern Duty
			 */
			case 0xFF16:
			sound_write_NR21(value8);
			return;

			/* NR22 - Sound Mode 2 register,
			 * Envelope
			 */
			case 0xFF17:
			sound_write_NR22(value8);
			return;

			/* NR23 - Sound Mode 2 register,
			 * Frequency lo data
			 */
			case 0xFF18:
			sound_write_NR23(value8);
			return;

			/* NR24 - Sound Mode 2 register,
			 * Frequency hi data
			 */
			case 0xFF19:
			sound_write_NR24(value8);
			return;

			/* NR30 - Sound Mode 3 register,
			 * Sound on/off
			 */
			case 0xFF1A:
			sound_write_NR30(value8);
			return;

			/* NR31 - Sound Mode 3 register,
			 * Sound length
			 */
			case 0xFF1B:
			sound_write_NR31(value8);
			return;

			/* NR32 - Sound Mode 3 register,
			 * Select output level
			 */
			case 0xFF1C:
			sound_write_NR32(value8);
			return;

			/* NR33 - Sound Mode 3 register,
			 * frequency's lower data
			 */
			case 0xFF1D:
			sound_write_NR33(value8);
			return;

			/* NR34 - Sound Mode 3 register,
			 * frequency's higher data
			 */
			case 0xFF1E:
			sound_write_NR34(value8);
			return;

			/* NR41 - Sound Mode 4 register,
			 * Sound length
			 */
			case 0xFF20:
			sound_write_NR41(value8);
			return;

			/* NR42 - Sound Mode 4 register,
			 * Envelope
			 */
			case 0xFF21:
			sound_write_NR42(value8);
			return;

			/* NR43 - Sound Mode 4 register,
			 * Polynomial counter
			 */
			case 0xFF22:
			sound_write_NR43(value8);
			return;

			/* NR44 - Sound Mode 4 register,
			 * Counter/consecutive; initial
			 */
			case 0xFF23:
			sound_write_NR44(value8);
			return;

			/* NR50 - Channel control / ON-OFF / Volume
			 */
			case 0xFF24:
			sound_write_NR50(value8);
			return;

			/* NR51 - Selection of Sound output terminal
			 */
			case 0xFF25:
			sound_write_NR51(value8);
			return;

			/* NR52 - Contents - Sound on/off
			 */
			case 0xFF26:
			sound_write_NR52(value8);
			return;

			/* Wave Pattern RAM
			 */
			case 0xFF30:
			case 0xFF31:
			case 0xFF32:
			case 0xFF33:
			case 0xFF34:
			case 0xFF35:
			case 0xFF36:
			case 0xFF37:
			case 0xFF38:
			case 0xFF39:
			case 0xFF3A:
			case 0xFF3B:
			case 0xFF3C:
			case 0xFF3D:
			case 0xFF3E:
			case 0xFF3F:
			sound_write_wavepattern(addr - 0xFF30, value8);
			return;

			/* LCDC - LCD Control
			 */
			case 0xFF40:
			gpu_write_lcdctrl(value8);
			return;

			/* STAT - LCD Status
			 */
			case 0xFF41:
			gpu_write_lcdstatus(value8);
			return;

			/* SCY - Scroll Y
			 */
			case 0xFF42:
			gpu_write_scrolly(value8);
			return;

			/* SCX - Scroll X
			 */
			case 0xFF43:
			gpu_write_scrollx(value8);
			return;

			/* LY - LCDC Y-Coordinate
			 */
			case 0xFF44:
			gpu_write_ly(value8);
			return;

			/* LYC - LY Compare
			 */
			case 0xFF45:
			gpu_write_lycmp(value8);
			return;

			/* DMA - DMA Transfer and Start Address
			 */
			case 0xFF46:
			gpu_start_dma(value8);
			return;

			/* BGP - Background and Window Palette Data
			 */
			case 0xFF47:
			gpu_write_bgp(value8);
			return;

			/* OBP0 - Object Palette 0 Data
			 */
			case 0xFF48:
			gpu_write_objpal0(value8);
			return;

			/* OBP1 - Object Palette 1 Data
			 */
			case 0xFF49:
			gpu_write_objpal1(value8);
			return;

			/* WY - Window Y Position
			 */
			case 0xFF4A:
			gpu_write_windowy(value8);
			return;

			/* WX - Window X Position
			 */
			case 0xFF4B:
			gpu_write_windowx(value8);
			return;

			default:
			mem.io[addr - 0xFF00] = value8;
			return;
		}
	}
	else if ( addr <= 0xFFFE )
	{
		mem.high_ram[addr - 0xFF80] = value8;
		return;
	}
	else
	{
		/* IE - Interrupt Enable
		 */
		interrupt_set_ie(value8);
	}
}

void mmu_write_mem16(uint16_t addr, uint16_t value16)
{
	mmu_write_mem8(addr, value16 & 0xFF);
	mmu_write_mem8(addr + 1, (value16 >> 8) & 0xFF);
}

int32_t mmu_dump_bios(const char *filename)
{
	FILE *f;
	f = fopen(filename, "w");
	if ( f == NULL )
		return -1;

	fwrite(bios, 1, sizeof(bios), f);
	fclose(f);

	return 0;
}

int32_t mmu_dump(FILE *file)
{
	if ( fwrite(&mem, 1, sizeof(mem), file) != sizeof(mem) )
		return -1;
	return 0;
}

int32_t mmu_restore(FILE *file)
{
	if ( fread(&mem, 1, sizeof(mem), file) != sizeof(mem) )
		return -1;
	return 0;
}
