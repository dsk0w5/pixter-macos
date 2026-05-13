#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "6502.h"



static inline void push(struct CPU *cpu, uint8_t v){
	
	memW(0x100 | (cpu->sp--), v);
}

static inline uint8_t pop(struct CPU *cpu){
	
	return memR(0x100 | (++cpu->sp));
}

static inline uint16_t read_16_pc(struct CPU *cpu){
	
	uint16_t ret = memR(cpu->pc++);
	
	ret |= ((uint16_t)memR(cpu->pc++)) << 8;
	
	return ret;
}

static inline uint16_t read_16(uint16_t addr){
	
	uint16_t ret = memR(addr++);
	
	ret |= ((uint16_t)memR(addr)) << 8;
	
	return ret;
}

static inline uint16_t ind_x(struct CPU *cpu){
	
	uint8_t addr = memR(cpu->pc++) + cpu->x;
	return (((uint16_t)memR(addr + 1)) << 8) | memR(addr);
}

static inline uint16_t ind_y(struct CPU *cpu){

	uint8_t addr = memR(cpu->pc++);
	return ((((uint16_t)memR(addr + 1)) << 8) | memR(addr)) + cpu->y;
}

static inline void set_nz(struct CPU *cpu, uint8_t v){
	
	cpu->sr = cpu->sr &~ (SR_N | SR_Z);
	if(!v) cpu->sr |= SR_Z;
	if(v & 0x80) cpu->sr |= SR_N;
}

#define VEC_NMI			0xFFFA
#define VEC_RST			0xFFFC
#define VEC_INT			0xFFFE

#define DIR_MODE_PRE		v = memR(cpu->pc++);
#define DIR_MODE_POST		Linker_error_no_dir_mode_writes();

#define A_MODE_EA
#define A_MODE_PRE			v = cpu->a;
#define A_MODE_POST			cpu->a = v;

#define ABS_MODE_EA			ea = read_16_pc(cpu);
#define ABS_MODE_PRE		ABS_MODE_EA	v = memR(ea);
#define ABS_MODE_POST		memW(ea, v);

#define ZPG_MODE_EA			t = memR(cpu->pc++);
#define ZPG_MODE_PRE		ZPG_MODE_EA	v = memR(t);
#define ZPG_MODE_POST		memW(t, v);

#define ZPG_X_MODE_EA		t = memR(cpu->pc++) + cpu->x;
#define ZPG_X_MODE_PRE		ZPG_X_MODE_EA	v = memR(t);
#define ZPG_X_MODE_POST		memW(t, v);

#define ZPG_Y_MODE_EA		t = memR(cpu->pc++) + cpu->y;
#define ZPG_Y_MODE_PRE		ZPG_Y_MODE_EA	v = memR(t);
#define ZPG_Y_MODE_POST		memW(t, v);

#define IND_X_MODE_EA		ea = ind_x(cpu);
#define IND_X_MODE_PRE		IND_X_MODE_EA	v = memR(ea);
#define IND_X_MODE_POST		memW(ea, v);


#define IND_Y_MODE_EA(pgCrossPenalty)		do { uint8_t addr = memR(cpu->pc++); uint16_t base = ((((uint16_t)memR(addr + 1)) << 8) | memR(addr)); ea = base + cpu->y; if (pgCrossPenalty && ((ea ^ base) >> 8)) cyDone++; } while (0);
#define IND_Y_MODE_PRE(pgCrossPenalty)		IND_Y_MODE_EA(pgCrossPenalty);	v = memR(ea);
#define IND_Y_MODE_POST						memW(ea, v);

#define ABS_XY_MODE_EA(w, pgCrossPenalty)	do { uint16_t base = read_16_pc(cpu); ea = base + cpu->w; if (pgCrossPenalty && ((ea ^ base) >> 8)) cyDone++; } while (0);

#define ABS_X_MODE_EA(pgCrossPenalty)		ABS_XY_MODE_EA(x, pgCrossPenalty)
#define ABS_X_MODE_PRE(pgCrossPenalty)		ABS_X_MODE_EA(pgCrossPenalty)	v = memR(ea);
#define ABS_X_MODE_POST						memW(ea, v);

#define ABS_Y_MODE_EA(pgCrossPenalty)		ABS_XY_MODE_EA(y, pgCrossPenalty)
#define ABS_Y_MODE_PRE						ABS_Y_MODE_EA(true); v = memR(ea);
#define ABS_Y_MODE_POST						memW(ea, v);


#define PUSH_RET(ofst)		push(cpu, (cpu->pc - (ofst)) >> 8); push(cpu, cpu->pc - (ofst));
#define POP_RET(ofst)		cpu->pc = pop(cpu); cpu->pc += ((uint16_t)pop(cpu)) << 8; cpu->pc += (ofst);


#define OP_ORA			set_nz(cpu, cpu->a |= v);
#define OP_AND			set_nz(cpu, cpu->a &= v);
#define OP_EOR			set_nz(cpu, cpu->a ^= v);
#define OP_ADC			if(cpu->sr & SR_D) {					\
					t16 = (uint16_t)(cpu->a & 0xF0) + (v & 0xF0);	\
					t = (cpu->a & 0x0F) + (v & 0x0F);		\
					cpu->sr &=~ (SR_C | SR_V | SR_N | SR_Z);	\
					if(!t && !(t16 & 0xFF))				\
						cpu->sr |= SR_Z;			\
					if(t > 9){					\
						t16 += 0x10;				\
						t += 6;					\
					}						\
					if(t16 & 0x80)					\
						cpu->sr |= SR_N;			\
					if(!(t16 & 0x180) || ((t16 & 0x180) == 0x180))	\
						cpu->sr |= SR_V;			\
					if(t16 > 0x90)					\
						t16 += 0x60;				\
					if(t16 & 0xff00)				\
						cpu->sr |= SR_C;			\
					t16 |= t;					\
					cpu->a = t16;					\
				} else{							\
					t16 = (uint16_t)cpu->a + (uint16_t)v;		\
					if(cpu->sr & SR_C) t16++;			\
					cpu->sr &=~ (SR_C | SR_V);			\
					if(t16 >= 0x100) cpu->sr |= SR_C;		\
					set_nz(cpu, cpu->a = t16);			\
					t16 &= 0x180;					\
					if(t16 && (t16 != 0x180))	cpu->sr |= SR_V;\
				}

#define OP_SBC			if(cpu->sr & SR_D) {					\
					uint16_t sum = cpu->a - v;			\
					uint8_t lo = (cpu->a & 0x0F) - (v & 0x0F);	\
					uint16_t hi = (cpu->a & 0xF0) - (v & 0xF0);	\
					if(!(cpu->sr & SR_C)){				\
						sum--;					\
						lo--;					\
					}						\
					cpu->sr &=~ (SR_C | SR_V | SR_N | SR_Z);	\
					if(lo & 0x10){					\
						lo -= 6;				\
						hi--;					\
					}						\
					if((cpu->a ^ v) & (cpu->a ^ sum) & 0x80)	\
						cpu->sr |= SR_V;			\
					if(hi & 0x100)					\
						hi -= 0x60;				\
					if(!(sum & 0xff00))				\
						cpu->sr |= SR_C;			\
					if(!(sum & 0xff))				\
						cpu->sr |= SR_Z;			\
					if(sum & 0x80)					\
						cpu->sr |= SR_N;			\
					cpu->a = (lo & 0x0F) | hi;			\
				} else{							\
					t16 = (uint16_t)cpu->a - (uint16_t)v;		\
					if(!(cpu->sr & SR_C)) t16--;			\
					cpu->sr &=~ (SR_C | SR_V);			\
					if(t16 < 0x100) cpu->sr |= SR_C;		\
					set_nz(cpu, cpu->a = t16);			\
					t16 &= 0x180;					\
					if(t16 && (t16 != 0x180))	cpu->sr |= SR_V;\
				}
#define OP_STA			v = cpu->a;
#define OP_STY			v = cpu->y;
#define OP_STX			v = cpu->x;
#define OP_LDA			set_nz(cpu, cpu->a = v);
#define OP_LDX			set_nz(cpu, cpu->x = v);
#define OP_LDY			set_nz(cpu, cpu->y = v);
#define OP_CP(r)		cpu->sr &=~ SR_C;				\
				if(cpu->r >= v) cpu->sr |= SR_C;		\
				set_nz(cpu, cpu->r - v);
#define OP_CPY			OP_CP(y)
#define OP_CPX			OP_CP(x)
#define OP_CMP			OP_CP(a)
#define OP_DEC			set_nz(cpu, --v);
#define OP_INC			set_nz(cpu, ++v);
#define OP_BIT			cpu->sr = cpu->sr &~ (SR_N | SR_Z | SR_V);	\
				if(!(v & cpu->a)) cpu->sr |= SR_Z;		\
				if(v & 0x80) cpu->sr |= SR_N;			\
				if(v & 0x40) cpu->sr |= SR_V;
#define OP_ASL			cpu->sr &=~ SR_C;				\
				if(v & 0x80) cpu->sr |= SR_C;			\
				set_nz(cpu, v <<= 1);
#define OP_ROL			ss = cpu->sr;					\
				cpu->sr &=~ SR_C;				\
				if(v & 0x80) cpu->sr |= SR_C;			\
				v <<= 1;					\
				if(ss & SR_C) v++;				\
				set_nz(cpu, v);
#define OP_ROR			ss = cpu->sr;					\
				cpu->sr &=~ SR_C;				\
				if(v & 0x01) cpu->sr |= SR_C;			\
				v >>= 1;					\
				if(ss & SR_C)  v |= 0x80;			\
				set_nz(cpu, v);
#define OP_LSR			cpu->sr &=~ SR_C;				\
				if(v & 1) cpu->sr |= SR_C;			\
				v >>= 1;
#define OP_B_COND_REL(c)	t = memR(cpu->pc++);					\
				if((c)) {											\
					uint16_t prevPc = cpu->pc;						\
					cpu->pc += (int16_t)(int8_t)t;					\
					cyDone += ((cpu->pc ^ prevPc) >> 8) ? 2 : 1;	\
				}

#define END(extraCy)		cyDone += extraCy; break;

#define VECTOR(save, v, set_i)		if (save) {					\
					PUSH_RET(0)				\
					push(cpu, cpu->sr);			\
				}						\
				if (set_i) cpu->sr |= SR_I;				\
				cpu->pc = read_16(v);
				




void cpuInit(struct CPU *cpu){
	
	cpu->in_intr = 0;
	cpu->sr |= SR_R;
	cpu->sp = 0xff;
	VECTOR(0, VEC_RST, false);
}

void cpuIrq(struct CPU *cpu, uint8_t on){

	cpu->in_intr = on;	
}

void cpuNmi(struct CPU *cpu){
	
	VECTOR(1, VEC_NMI, true);
}


unsigned cpuRun(struct CPU *cpu, unsigned cyRequested)
{
	unsigned cyDone = 0;

	while (cyDone < cyRequested){
		
		uint8_t t, v, ss, instr;
		uint16_t ea, t16;
		
		
		if(cpu->in_intr && !(cpu->sr & SR_I)) {

			VECTOR(1, VEC_INT, true);
			cyDone += 7;
		}
		instr = memR(cpu->pc++);

	/*
		fprintf(stderr, "[%04X] = %02X. SP=%02x [%02x %02x %02x %02x] A=%02x X=%02x Y=%02x\n", cpu->pc - 1, instr, cpu->sp,
				memR(0x100 | (uint8_t)(1 + cpu->sp)), memR(0x100 | (uint8_t)(2 + cpu->sp)),
				memR(0x100 | (uint8_t)(3 + cpu->sp)), memR(0x100 | (uint8_t)(4 + cpu->sp)), cpu->a, cpu->x, cpu->y);
	//*/

		if(cpu->sr & SR_D){
			fprintf(stderr,"decimal mode\n");
			exit(-6);
		}
	/*
		if (instr == 2) {
		//	instr is u16 at 0x98
		//	regs are 16xu32 at 0x40
			
			uint8_t regs[64], instr[2], i;

			for (i = 0; i < sizeof(regs); i++)
				regs[i] = memR(0x40 + i);
			for (i = 0; i < 2; i++)
				instr[i] = memR(0x98 + i);

			fprintf(stderr, "[%08x] = %04x, regs={", *(uint32_t*)(regs + 4 * 15) - 2, *(uint16_t*)instr);
			for (i = 0; i < 16; i++)
				fprintf(stderr, " r%02u = %08x", i, *(uint32_t*)(regs + 4 * i));
			fprintf(stderr, "}\n");

		} else
*/
		switch(instr){
			
			case 0x00:	cpu->pc++;							cpu->sr |= SR_B;	VECTOR(1, VEC_INT, false);			END(7)
			case 0x01:	IND_X_MODE_PRE						OP_ORA													END(6)
			case 0x05:	ZPG_MODE_PRE						OP_ORA													END(3)
			case 0x06:	ZPG_MODE_PRE						OP_ASL				ZPG_MODE_POST						END(5)
			case 0x08:	push(cpu, cpu->sr);																			END(3)
			case 0x09:	DIR_MODE_PRE						OP_ORA													END(2)
			case 0x0A:	A_MODE_PRE	OP_ASL					A_MODE_POST												END(2)
			case 0x0D:	ABS_MODE_PRE						OP_ORA													END(4)
			case 0x0E:	ABS_MODE_PRE						OP_ASL				ABS_MODE_POST						END(6)
			case 0x10:	OP_B_COND_REL(!(cpu->sr & SR_N))															END(2)
			case 0x11:	IND_Y_MODE_PRE(true)				OP_ORA													END(5)
			case 0x15:	ZPG_X_MODE_PRE						OP_ORA													END(4)
			case 0x16:	ZPG_X_MODE_PRE						OP_ASL				ZPG_X_MODE_POST						END(6)
			case 0x18:	cpu->sr &=~ SR_C;																			END(2)
			case 0x19:	ABS_Y_MODE_PRE						OP_ORA													END(4)
			case 0x1D:	ABS_X_MODE_PRE(true)				OP_ORA													END(4)
			case 0x1E:	ABS_X_MODE_PRE(false)				OP_ASL				ABS_X_MODE_POST						END(7)
			case 0x20:	ea = read_16_pc(cpu);				PUSH_RET(1);		cpu->pc = ea;						END(6)
			case 0x21:	IND_X_MODE_PRE						OP_AND													END(6)
			case 0x24:	ZPG_MODE_PRE						OP_BIT													END(3)
			case 0x25:	ZPG_MODE_PRE						OP_AND													END(3)
			case 0x26:	ZPG_MODE_PRE						OP_ROL				ZPG_MODE_POST						END(5)
			case 0x28:	cpu->sr = pop(cpu) &~ (SR_B | SR_R);														END(4)
			case 0x29:	DIR_MODE_PRE						OP_AND													END(2)
			case 0x2A:	A_MODE_PRE							OP_ROL				A_MODE_POST							END(2)
			case 0x2C:	ABS_MODE_PRE						OP_BIT													END(4)
			case 0x2D:	ABS_MODE_PRE						OP_AND													END(4)
			case 0x2E:	ABS_MODE_PRE						OP_ROL				ABS_MODE_POST						END(6)
			case 0x30:	OP_B_COND_REL(cpu->sr & SR_N)																END(2)
			case 0x31:	IND_Y_MODE_PRE(true)				OP_AND													END(5)
			case 0x35:	ZPG_X_MODE_PRE						OP_AND													END(4)
			case 0x36:	ZPG_X_MODE_PRE						OP_ROL				ZPG_X_MODE_POST						END(6)
			case 0x38:	cpu->sr |= SR_C;																			END(2)
			case 0x39:	ABS_Y_MODE_PRE						OP_AND													END(4)
			case 0x3D:	ABS_X_MODE_PRE(true)				OP_AND													END(4)
			case 0x3E:	ABS_X_MODE_PRE(true)				OP_ROL				ABS_X_MODE_POST						END(6)
			case 0x40:	cpu->sr = pop(cpu)&~ (SR_B | SR_R);	POP_RET(0)												END(6)
			case 0x41:	IND_X_MODE_PRE						OP_EOR													END(6)
			case 0x45:	ZPG_MODE_PRE						OP_EOR													END(3)
			case 0x46:	ZPG_MODE_PRE						OP_LSR				ZPG_MODE_POST						END(5)
			case 0x48:	push(cpu, cpu->a);																			END(3)
			case 0x49:	DIR_MODE_PRE						OP_EOR													END(2)
			case 0x4A:	A_MODE_PRE	OP_LSR					A_MODE_POST												END(2)
			case 0x4C:	cpu->pc = read_16_pc(cpu);																	END(3)
			case 0x4D:	ABS_MODE_PRE						OP_EOR													END(4)
			case 0x4E:	ABS_MODE_PRE						OP_LSR				ABS_MODE_POST						END(6)
			case 0x50:	OP_B_COND_REL(!(cpu->sr & SR_V))															END(2)
			case 0x51:	IND_Y_MODE_PRE(true)				OP_EOR													END(5)
			case 0x55:	ZPG_X_MODE_PRE						OP_EOR													END(4)
			case 0x56:	ZPG_X_MODE_PRE						OP_LSR				ZPG_X_MODE_POST						END(6)
			case 0x58:	cpu->sr &=~ SR_I;																			END(2)
			case 0x59:	ABS_Y_MODE_PRE						OP_EOR													END(4)
			case 0x5D:	ABS_X_MODE_PRE(true)				OP_EOR													END(4)
			case 0x5E:	ABS_X_MODE_PRE(false)				OP_LSR				ABS_X_MODE_POST						END(7)
			case 0x60:	POP_RET(1)																					END(6)
			case 0x61:	IND_X_MODE_PRE						OP_ADC													END(6)
			case 0x65:	ZPG_MODE_PRE						OP_ADC													END(3)
			case 0x66:	ZPG_MODE_PRE						OP_ROR				ZPG_MODE_POST						END(5)
			case 0x68:	set_nz(cpu, cpu->a = pop(cpu));																END(4)
			case 0x69:	DIR_MODE_PRE						OP_ADC													END(2)
			case 0x6A:	A_MODE_PRE							OP_ROR				A_MODE_POST							END(2)
			case 0x6C:	cpu->pc = read_16(read_16_pc(cpu));															END(5)
			case 0x6D:	ABS_MODE_PRE						OP_ADC													END(4)
			case 0x6E:	ABS_MODE_PRE						OP_ROR				ABS_MODE_POST						END(6)
			case 0x70:	OP_B_COND_REL(cpu->sr & SR_V)																END(2)
			case 0x71:	IND_Y_MODE_PRE(true)				OP_ADC													END(5)
			case 0x75:	ZPG_X_MODE_PRE						OP_ADC													END(4)
			case 0x76:	ZPG_X_MODE_PRE						OP_ROR				ZPG_X_MODE_POST						END(5)
			case 0x78:	cpu->sr |= SR_I;																			END(2)
			case 0x79:	ABS_Y_MODE_PRE						OP_ADC													END(4)
			case 0x7D:	ABS_X_MODE_PRE(true)				OP_ADC													END(4)
			case 0x7E:	ABS_X_MODE_PRE(false)				OP_ROR				ABS_X_MODE_POST						END(7)
			case 0x81:	IND_X_MODE_EA						OP_STA				IND_X_MODE_POST						END(6)
			case 0x84:	ZPG_MODE_EA							OP_STY				ZPG_MODE_POST						END(3)
			case 0x85:	ZPG_MODE_EA							OP_STA				ZPG_MODE_POST						END(4)
			case 0x86:	ZPG_MODE_EA							OP_STX				ZPG_MODE_POST						END(3)
			case 0x88:	set_nz(cpu, --cpu->y);																		END(2)
			case 0x8A:	set_nz(cpu, cpu->a = cpu->x);																END(2)
			case 0x8C:	ABS_MODE_EA							OP_STY				ABS_MODE_POST						END(4)
			case 0x8D:	ABS_MODE_EA							OP_STA				ABS_MODE_POST						END(4)
			case 0x8E:	ABS_MODE_EA							OP_STX				ABS_MODE_POST						END(4)
			case 0x90:	OP_B_COND_REL(!(cpu->sr & SR_C))															END(2)
			case 0x91:	IND_Y_MODE_EA(false)				OP_STA				IND_Y_MODE_POST						END(6)
			case 0x94:	ZPG_X_MODE_EA						OP_STY				ZPG_X_MODE_POST						END(4)
			case 0x95:	ZPG_X_MODE_EA						OP_STA				ZPG_X_MODE_POST						END(4)
			case 0x96:	ZPG_Y_MODE_EA						OP_STX				ZPG_Y_MODE_POST						END(4)
			case 0x98:	set_nz(cpu, cpu->a = cpu->y);																END(2)
			case 0x99:	ABS_Y_MODE_EA(false)				OP_STA				ABS_Y_MODE_POST						END(5)
			case 0x9A:	cpu->sp = cpu->x;																			END(2)
			case 0x9D:	ABS_X_MODE_EA(false)				OP_STA				ABS_X_MODE_POST						END(5)
			case 0xA0:	DIR_MODE_PRE						OP_LDY													END(2)
			case 0xA1:	IND_X_MODE_PRE						OP_LDA													END(6)
			case 0xA2:	DIR_MODE_PRE						OP_LDX													END(2)
			case 0xA4:	ZPG_MODE_PRE						OP_LDY													END(3)
			case 0xA5:	ZPG_MODE_PRE						OP_LDA													END(3)
			case 0xA6:	ZPG_MODE_PRE						OP_LDX													END(3)
			case 0xA8:	set_nz(cpu, cpu->y = cpu->a);																END(2)
			case 0xA9:	DIR_MODE_PRE						OP_LDA													END(2)
			case 0xAA:	set_nz(cpu, cpu->x = cpu->a);																END(2)
			case 0xAC:	ABS_MODE_PRE						OP_LDY													END(4)
			case 0xAD:	ABS_MODE_PRE						OP_LDA													END(4)
			case 0xAE:	ABS_MODE_PRE						OP_LDX													END(4)
			case 0xB0:	OP_B_COND_REL(cpu->sr & SR_C)																END(2)
			case 0xB1:	IND_Y_MODE_PRE(true)				OP_LDA													END(5)
			case 0xB4:	ZPG_X_MODE_PRE						OP_LDY													END(4)
			case 0xB5:	ZPG_X_MODE_PRE						OP_LDA													END(4)
			case 0xB6:	ZPG_Y_MODE_PRE						OP_LDX													END(4)
			case 0xB8:	cpu->sr &=~ SR_V;																			END(2)
			case 0xB9:	ABS_Y_MODE_PRE						OP_LDA													END(4)
			case 0xBA:	set_nz(cpu, cpu->x = cpu->sp);																END(2)
			case 0xBC:	ABS_X_MODE_PRE(true)				OP_LDY													END(4)
			case 0xBD:	ABS_X_MODE_PRE(true)				OP_LDA													END(4)
			case 0xBE:	ABS_Y_MODE_PRE						OP_LDX													END(4)
			case 0xC0:	DIR_MODE_PRE						OP_CPY													END(2)
			case 0xC1:	IND_X_MODE_PRE						OP_CMP													END(6)
			case 0xC4:	ZPG_MODE_PRE						OP_CPY													END(3)
			case 0xC5:	ZPG_MODE_PRE						OP_CMP													END(3)
			case 0xC6:	ZPG_MODE_PRE						OP_DEC				ZPG_MODE_POST						END(5)
			case 0xC8:	set_nz(cpu, ++cpu->y);																		END(2)
			case 0xC9:	DIR_MODE_PRE						OP_CMP													END(2)
			case 0xCA:	set_nz(cpu, --cpu->x);																		END(2)
			case 0xCC:	ABS_MODE_PRE						OP_CPY													END(4)
			case 0xCD:	ABS_MODE_PRE						OP_CMP													END(4)
			case 0xCE:	ABS_MODE_PRE						OP_DEC				ABS_MODE_POST						END(6)
			case 0xD0:	OP_B_COND_REL(!(cpu->sr & SR_Z))															END(2)
			case 0xD1:	IND_Y_MODE_PRE(true)				OP_CMP													END(5)
			case 0xD5:	ZPG_X_MODE_PRE						OP_CMP													END(4)
			case 0xD6:	ZPG_X_MODE_PRE						OP_DEC				ZPG_X_MODE_POST						END(6)
			case 0xD8:	cpu->sr &=~ SR_D;																			END(2)
			case 0xD9:	ABS_Y_MODE_PRE						OP_CMP													END(4)
			case 0xDD:	ABS_X_MODE_PRE(true)				OP_CMP													END(4)
			case 0xDE:	ABS_X_MODE_PRE(false)				OP_DEC				ABS_Y_MODE_POST						END(7)
			case 0xE0:	DIR_MODE_PRE						OP_CPX													END(2)
			case 0xE1:	IND_X_MODE_PRE						OP_SBC													END(6)
			case 0xE4:	ZPG_MODE_PRE						OP_CPX													END(3)
			case 0xE5:	ZPG_MODE_PRE						OP_SBC													END(3)
			case 0xE6:	ZPG_MODE_PRE						OP_INC				ZPG_MODE_POST						END(5)
			case 0xE8:	set_nz(cpu, ++cpu->x);																		END(2)
			case 0xE9:	DIR_MODE_PRE						OP_SBC													END(2)
			case 0xEA:																								END(2)
			case 0xEC:	ABS_MODE_PRE						OP_CPX													END(4)
			case 0xED:	ABS_MODE_PRE						OP_SBC													END(4)
			case 0xEE:	ABS_MODE_PRE						OP_INC				ABS_MODE_POST						END(6)
			case 0xF0:	OP_B_COND_REL(cpu->sr & SR_Z)																END(2)
			case 0xF1:	IND_Y_MODE_PRE(true)				OP_SBC													END(5)
			case 0xF5:	ZPG_X_MODE_PRE						OP_SBC													END(4)
			case 0xF6:	ZPG_X_MODE_PRE						OP_INC				ZPG_X_MODE_POST						END(6)
			case 0xF8:	cpu->sr |= SR_D;																			END(2)
			case 0xF9:	ABS_Y_MODE_PRE						OP_SBC													END(4)
			case 0xFD:	ABS_X_MODE_PRE(true)				OP_SBC													END(4)
			case 0xFE:	ABS_X_MODE_PRE(false)				OP_INC				ABS_Y_MODE_POST						END(7)

			default:
				fprintf(stderr, "unknown instr %02X\n", instr);
				exit(-5);
				break;
		}
	}
	return cyDone;
}
