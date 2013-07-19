#ifndef _Z80_H_
#define _Z80_H_

/* z80 registers
 */
struct z80_cpu
{
#define Z80_REG(REG_HI, REG_LO)			\
	union					\
	{					\
		uint16_t REG_HI##REG_LO;	\
		struct {			\
			uint8_t REG_LO;		\
			uint8_t REG_HI;		\
		};				\
	}
	Z80_REG(A, F);
	Z80_REG(B, C);
	Z80_REG(D, E);
	Z80_REG(H, L);
	uint16_t SP, PC;

	/* enable and disable interrupts requests
	 */
	uint32_t enable_interrupt, disable_interrupt;

	/* z80 is halted until an interruption occurs
	 */
	uint32_t halted;

	/* z80 is stopped until a button is pressed
	 */
	uint32_t stopped;
};

int32_t z80_init(void);
void z80_run(void);

void z80_call(uint16_t addr);
void z80_resume_stop(void);
void z80_resume_halt(void);

uint32_t z80_halted(void);
uint32_t z80_stopped(void);

uint32_t z80_next_opcode(uint32_t disassemble);

int32_t z80_dump(FILE *file);
int32_t z80_restore(FILE *file);


#endif
