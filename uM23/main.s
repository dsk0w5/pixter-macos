
 .feature c_comments


IDX_INITIAL_SP			= $00
IDX_INITIAL_PC			= $01
IDX_NMI					= $02
IDX_HARD_FAULT			= $03
IDX_MEM_MANAGE_FAULT	= $04
IDX_BUS_FAULT			= $05
IDX_USAGE_FAULT			= $06
IDX_SECURE_FAULT		= $07
IDX_SVCALL				= $0b
IDX_DEBUG				= $0c
IDX_PENDSV				= $0e
IDX_SYSTICK				= $0f
IDX_FIRST_IRQ			= $10


DISPLAY		= $0CE0				;4E0 for classic. wish i knew how to tell them apart

REGS		= $40				;must be 64-byte aligned
VTOR		= $80				;WRITTEN BY GUEST DIRECTLY: we assume our VTOR is 256-byte aligned and do not even read lower byte (same as arm)
G_IRQS_PEND	= $84				;WRITTEN BY GUEST DIRECTLY: mask of irqs pending to guest (not all may be unmasked)
G_IRQS_ENA	= $85				;WRITTEN BY GUEST DIRECTLY: guest irq mask. not live checked so gues has to dis and ena IRQs globally or wait for irq to happen to have effect
FLAG_C		= $86				;C in bottom bit, rest is DNK
FLAG_N		= $87				;N in top bit, rest is DNK
FLAG_V		= $88				;nonzero on V
FLAG_Z		= $89				;ORR of all produced bytez, zero if Z flag would be set
TEMP12		= $8a				;12 bytes of temp values for mul/div/misc
TEMPA		= $96				;order of this and TMPB is assumed and together they are used as jump addr storage
TEMPB		= $97				;order of this and TMPB is assumed and together they are used as jump addr storage
INSTR		= $98				;first halfword of instr
INTERP		= $9a				;pointer to interp loop stored here, changed by irq handlers to point to irq handling code
RDPTR		= $9c
RMPTR		= $9e
RNPTR		= $a0
IPSR		= $a2
IRQSON		= $a3				;mirror of 6502 state since it is not easy to get
IRQMASK		= $a4				;mirror of register $01 since we canot read it
RTC			= $a5				;u32
TICK_HI		= $a9				;u24
FOSC_RATE	= $ac				;u16 (Fosc ticks per 1/128 of a second)

;we do not support process stack, priviledge modes, we do not disable IRQs for exception entry but do for irq handler entry
;we do not support systick, and our VTOR is unusual (being at 0x40000080)
;our excpetions preserve "Were irqs on" state

.segment "CODE"

emu_entry:					;must start at start of segment	
	JMP emu_init

.align $20					;go to 0x$2020 where our irq handler should be
	PHA
	TXA
	PHA
	TYA
	PHA
	LDX $01
	TXA
	AND #$08
	BEQ no_tbh_irq
	STA $02
	INC RTC + 0
	BNE no_tbh_irq
	INC RTC + 1
	BNE no_tbh_irq
	INC RTC + 2
	BNE no_tbh_irq
	INC RTC + 3
no_tbh_irq:
	TXA
	AND #$10
	BEQ no_rosc_irq
	STA $02
	INC TICK_HI + 0
	BNE no_rosc_irq
	INC TICK_HI + 1
	BNE no_rosc_irq
	INC TICK_HI + 2
no_rosc_irq:

	;others - pend them to ARM
	TXA
	AND #$e5
	STA $02
	ORA G_IRQS_PEND
	STA G_IRQS_PEND
	JSR irqs_recalc

	PLA
	TAY
	PLA
	TAX
	PLA
	RTI


emu_init:
	SEI
	;calc Fosc rate once since later it'll mess up timer stuffs
	JSR fosc_meas_setup
	JSR fosc_meas_wait_count_ends          ;we may be in the counting region or we may not be. wait for counting to end, if we are not in the counting region, we may be about to be
    JSR fosc_meas_wait_count_starts
    JSR fosc_meas_wait_count_ends          ;we are now synchronized to START of no-counting region
    JSR fosc_meas_setup
    JSR fosc_meas_wait_count_starts
    JSR fosc_meas_wait_count_ends
    LDA $11         ;hi
    LDX $10         ;lo
	STA FOSC_RATE + 1
	STX FOSC_RATE + 0

	;relocate irq handler to to cart, clear all irqs
	LDA $0b
	ORA #$02
	STA $0b
	LDA #$ff
	STA $02

	;config our timers now to count ticks (T0) and RTC (TBH), T1 left for future use to implement alarms, set up IRQs
	LDA #$03	;1khz irq for TBH
	STA $0c
	LDA #$20	;TM0 at Rosc, TM1 off
	STA $0f
	LDA #$18	;TBH and TM0 irqs
	STA IRQMASK
	STA $01

	;VTOR stars pointing to start of cart page 0, which is ARM VA 0x80600000 (offset 0 in bank 0xc0)
	LDA #$80
	STA VTOR + 3
	LDA #$60
	STA VTOR + 2
	LDA #0
	STA VTOR + 1
	STA VTOR + 0
	STA G_IRQS_PEND
	STA G_IRQS_ENA
	STA TICK_HI + 0
	STA TICK_HI + 1
	STA TICK_HI + 2
	STA RTC + 0
	STA RTC + 1
	STA RTC + 2
	STA RTC + 3

	;init emu state
	STA RDPTR + 1
	STA RNPTR + 1
	STA RMPTR + 1
	STA IPSR
	LDA #1
	STA IRQSON

	;load SP and PC from VTOR
	LDX #REGS + $34
	LDA #$ff
	LDY #0
	JSR vector_read_ex

	LDY #4
	JSR vector_read_to_pc
;	CLI	;we are ready for irqs

	JMP do_next_instr_reset_interp_ptr

do_irq_handle:			
	;INTERP will point here if we have a pending irq to deliver to the guest and we'll restore it to "&do_next_instr" when handled
	;there may be more than one, so we just deliver the highest prio one
	LDA IRQSON
	BEQ do_next_instr_reset_interp_ptr

	LDA G_IRQS_PEND
	AND G_IRQS_ENA
	BEQ do_next_instr_reset_interp_ptr	;nothing anymore?

	LDX #IDX_FIRST_IRQ
	STX TEMPA

irq_find:
	ROR
	BCS irq_found
	INC TEMPA
	JMP irq_find

irq_found:
	LDA TEMPA
	JSR take_exc

do_next_instr_reset_interp_ptr:
	PHP
	SEI
	LDA #>do_next_instr
	STA INTERP + 1
	LDA #<do_next_instr
	STA INTERP + 0
	PLP
	;fallthrough

do_next_instr:			;INTERP will usually point here

	LDX #INSTR
	JSR fetch_instr_halfword

;	JSR uartDebug

;	.byte $02
	LDA INSTR + 1
	AND #$fe
	TAX
	LDA dispatch_main, X
	STA TEMPA
	LDA dispatch_main + 1, X
	STA TEMPB
	JMP (TEMPA)

j_udf16_2:
	JMP udf_at_pc_minus_2

instr_svc:
	ROR INSTR + 1
	BCS j_udf16_2
	LDX #IDX_SVCALL
	JMP exc_at_pc_minus_2
	
instr_cps:
	LDA INSTR + 1
	CMP #$b6
	BNE j_udf16_2
	LDA INSTR + 0
	AND #$ef
	CMP #$62
	BNE j_udf16_2
	CMP INSTR + 0
	BEQ instr_cpsie

instr_cpsid:
	LDA #0
	STA IRQSON
	JSR irqs_recalc
	JMP (TEMPA)

instr_cpsie:
	STA IRQSON		;currently A is nonzero
	JSR irqs_recalc
	JMP (TEMPA)

instr_lsl2:
	JSR get_rm_loreg_3
	JSR get_rd_loreg_0
	LDX RDPTR + 0
	LDY #0
	LDA (RMPTR), Y
	BNE instr_lsl				;might do a slow shift by 255 here, oh well...
	BEQ instr_shifts_set_NZ

instr_lsl1_imm:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	JSR copy_rm_to_rd
	JSR get_immX_6
	LDX RDPTR + 0
	AND #$1f
	BEQ instr_shifts_set_NZ

instr_lsl:				;value by is in A, 1+, X is RDPTR
	TAY
lsl_loop:
	ASL 0, X
	ROL 1, X
	ROL 2, X
	ROL 3, X
	DEY
	BNE lsl_loop

instr_shifts_set_CNZ:			;assumes C is in C
	ROL
	STA FLAG_C

instr_shifts_set_NZ:
	LDA 3, X
	STA FLAG_N
	ORA 2, X
	ORA 1, X
	ORA 0, X
	STA FLAG_Z
	JMP (INTERP)

instr_ror:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDX RMPTR + 0
	LDA 0, Y
	BEQ instr_shifts_set_NZ
	AND #$1f
	BEQ instr_ror_0
	TAY
	LDA 0, X
	ROL
ror_loop:
	ROR 3, X
	ROR 2, X
	ROR 1, X
	ROR 0, X
	DEY
	BNE ror_loop
	BEQ instr_shifts_set_CNZ

instr_ror_0:
	LDA 3, X
	ROL
	JMP instr_shifts_set_CNZ

instr_lsr2:
	LDA #0
	STA TEMPA
	JSR get_rm_loreg_3
	JSR get_rd_loreg_0
	LDX RDPTR + 0
	LDY #0
	LDA (RMPTR), Y
	BNE instr_Xsr				;might do a slow shift by 255 here, oh well...
	BEQ instr_shifts_set_NZ

instr_asr2:
	LDA #$80
	STA TEMPA
	JSR get_rm_loreg_3
	JSR get_rd_loreg_0
	LDX RDPTR + 0
	LDY #0
	LDA (RMPTR), Y
	BNE instr_Xsr				;might do a slow shift by 255 here, oh well...
	BEQ instr_shifts_set_NZ

instr_asr1_imm:
	LDA #$80
	STA TEMPA
	BNE instr_Xsr1_imm	;faster than jmp

instr_lsr1_imm:
	LDA #0
	STA TEMPA

instr_Xsr1_imm:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	JSR copy_rm_to_rd
	JSR get_immX_6
	AND #$1f
	LDX RDPTR + 0
	BEQ instr_Xsr_32

instr_Xsr:
	TAY
Xsr_loop:
	LDA TEMPA
	AND 3, X
	ROL
	ROR 3, X
	ROR 2, X
	ROR 1, X
	ROR 0, X
	DEY
	BNE Xsr_loop
	ROL
	STA FLAG_C
	JMP instr_shifts_set_NZ

instr_Xsr_32:
	ROL 3, X		;top bit to carry
	
	LDA TEMPA
	AND 3, X
	BEQ instr_Xsr_32_zero
	LDA #$ff
	BNE instr_Xsr_32_got_val	;shorter than JMP

instr_Xsr_32_zero:
	LDA #0

instr_Xsr_32_got_val:
	STA 0, X
	STA 1, X
	STA 2, X
	STA 3, X
	STA FLAG_N
	STA FLAG_Z
	ROL				;get carry (we havent corrupted it)
	STA FLAG_C
	JMP (INTERP)



instr_add3:
	JSR get_rm_loreg_6
	JSR get_rn_loreg_3
	JSR get_rd_loreg_0
	LDY #0
	STY FLAG_Z
	LDX #4
add_3op_no_carry_input:
	CLC
add_3op_loop:
	LDA (RNPTR), Y
	ADC (RMPTR), Y
	STA (RDPTR), Y
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	INY
	DEX
	BNE add_3op_loop

set_c_v_after_alu_specific:		;specific way that is common to a few ops. expects V set correctly and will capture C from C, Z assumed set

	ROL
	STA FLAG_C
	BVS add_v_set
	LDA #0
	STA FLAG_V
	JMP (INTERP)
add_v_set:
	LDA #1
	STA FLAG_V
	JMP (INTERP)

instr_sub3:
	LDA #0
	STA FLAG_Z
	JSR get_rm_loreg_6
	JSR get_rn_loreg_3
	JSR get_rd_loreg_0
sub_3op_no_carry_input:
	LDY #0
	LDX #4
	SEC
sub_3op_loop:
	LDA (RNPTR), Y
	SBC (RMPTR), Y
	STA (RDPTR), Y
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	INY
	DEX
	BNE sub_3op_loop
	BEQ set_c_v_after_alu_specific		;faster than jump

instr_add1:
	LDA #0
	STA FLAG_Z
	JSR get_rn_loreg_3
	JSR get_rd_loreg_0
	JSR get_immX_6
	AND #$07
	LDX RDPTR + 0
	LDY RNPTR + 0
	CLC
	ADC 0, Y
	STA 0, X
	ORA FLAG_Z
	STA FLAG_Z
	LDA 1, Y
	ADC #0
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z
	LDA 2, Y
	ADC #0
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z
	LDA 3, Y
	ADC #0
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP set_c_v_after_alu_specific

instr_sub1:
	LDA #0
	STA FLAG_Z
	JSR get_rn_loreg_3
	JSR get_rd_loreg_0
	JSR get_immX_6
	AND #$07
	EOR #$ff
	LDX RDPTR + 0
	LDY RNPTR + 0
	SEC
	ADC 0, Y
	STA 0, X
	ORA FLAG_Z
	STA FLAG_Z
	LDA 1, Y
	SBC #0
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z
	LDA 2, Y
	SBC #0
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z
	LDA 3, Y
	SBC #0
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP set_c_v_after_alu_specific

instr_mov1:
	JSR get_rd_loreg_8
	LDX RDPTR + 0
	LDA INSTR + 0
	STA 0, X
	STA FLAG_Z
	LDA #0
	STA 1, X
	STA 2, X
	STA 3, X
	STA FLAG_N
	JMP (INTERP)

instr_cmp1:
	JSR get_rd_loreg_8
	LDX RDPTR + 0
	SEC
	LDA 0, X
	SBC INSTR + 0
	STA FLAG_Z

	LDA 1, X
	SBC #0
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, X
	SBC #0
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, X
	SBC #0
	STA FLAG_N
	JMP set_c_v_after_alu_specific

instr_add2:
	LDA #0
	STA FLAG_Z
	JSR get_rd_loreg_8
	LDX RDPTR + 0
	CLC
	LDA INSTR + 0
	ADC 0, X
	STA 0, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA #0
	ADC 1, X
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA #0
	ADC 2, X
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA #0
	ADC 3, X
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP set_c_v_after_alu_specific

instr_sub2:
	LDA #0
	STA FLAG_Z
	JSR get_rd_loreg_8
	LDX RDPTR + 0
	SEC
	LDA 0, X
	SBC INSTR + 0
	STA 0, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 1, X
	SBC #0
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, X
	SBC #0
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, X
	SBC #0
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP set_c_v_after_alu_specific


instrs_dp_reg:
	LDA INSTR + 0
	ROL
	TAX
	ROL INSTR + 1
	TXA
	ROL
	LDA INSTR + 1
	ROL
	ROL
	AND #$1e
	TAX
	LDA dispatch_dp_reg, X
	STA TEMPA
	LDA dispatch_dp_reg + 1, X
	STA TEMPB
	JMP (TEMPA)
	
instr_eor:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDY RMPTR + 0
	LDX RDPTR
	LDY RMPTR

	LDA 0, X
	EOR 0, Y
	STA 0, X
	STA FLAG_Z

	LDA 1, X
	EOR 1, Y
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, X
	EOR 2, Y
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, X
	EOR 3, Y
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP (INTERP)

instr_orr:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDY RMPTR + 0
	LDX RDPTR
	LDY RMPTR

	LDA 0, X
	ORA 0, Y
	STA 0, X
	STA FLAG_Z

	LDA 1, X
	ORA 1, Y
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, X
	ORA 2, Y
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, X
	ORA 3, Y
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP (INTERP)

instr_bic:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDY RMPTR + 0
	LDX RDPTR
	LDY RMPTR

	LDA 0, Y
	EOR #$ff
	AND 0, X
	STA 0, X
	STA FLAG_Z

	LDA 1, Y
	EOR #$ff
	AND 1, X
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, Y
	EOR #$ff
	AND 2, X
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, Y
	EOR #$ff
	AND 3, X
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP (INTERP)

instr_mvn:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDY RMPTR + 0
	LDX RDPTR
	LDY RMPTR

	LDA 0, Y
	EOR #$ff
	STA 0, X
	STA FLAG_Z

	LDA 1, Y
	EOR #$ff
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, Y
	EOR #$ff
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, Y
	EOR #$ff
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP (INTERP)

instr_and:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDY RMPTR + 0
	LDX RDPTR
	LDY RMPTR

	LDA 0, X
	AND 0, Y
	STA 0, X
	STA FLAG_Z

	LDA 1, X
	AND 1, Y
	STA 1, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, X
	AND 2, Y
	STA 2, X
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, X
	AND 3, Y
	STA 3, X
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP (INTERP)

instr_tst:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR + 0
	LDY RMPTR + 0
	
	LDA 0, X
	AND 0, Y
	STA FLAG_Z

	LDA 1, X
	AND 1, Y
	ORA FLAG_Z
	STA FLAG_Z

	LDA 2, X
	AND 2, Y
	ORA FLAG_Z
	STA FLAG_Z

	LDA 3, X
	AND 3, Y
	STA FLAG_N
	ORA FLAG_Z
	STA FLAG_Z
	JMP (INTERP)

instr_mul:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDA #TEMP12 + 0		; +0 = cur add mask
	STA RNPTR + 0
	LDA #TEMP12 + 4		; +4 = cur remaining mul-by mask, inverted to save one cycle in the loop (it will pre-carry clear for us)
	STA TEMPA
	LDA #0
	STA TEMPB

	LDY #3
mul_init_loop:
	LDA (RDPTR), Y
	STA (RNPTR), Y
	LDA (RMPTR), Y
	EOR #$ff
	STA (TEMPA), Y
	LDA #0
	STA (RDPTR), Y		;zero Rd
	DEY
	BPL mul_init_loop

	LDX RDPTR + 0

	LDY #8
mul_do_loop_32:
	ROR TEMP12 + 7
	ROR TEMP12 + 6
	ROR TEMP12 + 5
	ROR TEMP12 + 4
	BCS mul_add_done_32	;carry is clear if we need to add
	LDA TEMP12 + 0
	ADC 0, X
	STA 0, X
	LDA TEMP12 + 1
	ADC 1, X
	STA 1, X
	LDA TEMP12 + 2
	ADC 2, X
	STA 2, X
	LDA TEMP12 + 3
	ADC 3, X
	STA 3, X
mul_add_done_32:
	ASL TEMP12 + 0
	ROL TEMP12 + 1
	ROL TEMP12 + 2
	ROL TEMP12 + 3
	DEY
	BNE mul_do_loop_32


	LDY #8
mul_do_loop_24:
	ROR TEMP12 + 6
	ROR TEMP12 + 5
	ROR TEMP12 + 4
	BCS mul_add_done_24	;carry is clear if we need to add
	LDA TEMP12 + 1
	ADC 1, X
	STA 1, X
	LDA TEMP12 + 2
	ADC 2, X
	STA 2, X
	LDA TEMP12 + 3
	ADC 3, X
	STA 3, X
mul_add_done_24:
	ASL TEMP12 + 1
	ROL TEMP12 + 2
	ROL TEMP12 + 3
	DEY
	BNE mul_do_loop_24

	LDY #8
mul_do_loop_16:
	ROR TEMP12 + 5
	ROR TEMP12 + 4
	BCS mul_add_done_16	;carry is clear if we need to add
	LDA TEMP12 + 2
	ADC 2, X
	STA 2, X
	LDA TEMP12 + 3
	ADC 3, X
	STA 3, X
mul_add_done_16:
	ASL TEMP12 + 2
	ROL TEMP12 + 3
	DEY
	BNE mul_do_loop_16

	LDY #8
mul_do_loop_8:
	ROR TEMP12 + 4
	BCS mul_add_done_8	;carry is clear if we need to add
	LDA TEMP12 + 3
	ADC 3, X
	STA 3, X
mul_add_done_8:
	ASL TEMP12 + 3
	DEY
	BNE mul_do_loop_8



	JMP instr_shifts_set_NZ

instr_adc:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDA RDPTR
	STA RNPTR
	LDA FLAG_C
	ROR
	JMP add_3op_loop

instr_sbc:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDA RDPTR
	STA RNPTR
	LDA FLAG_C
	ROR
	JMP sub_3op_loop			;N - M -> D

instr_neg:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDA #TEMP12 + 0
	STA RNPTR
	LDA #0
	STA TEMP12 + 0
	STA TEMP12 + 1
	STA TEMP12 + 2
	STA TEMP12 + 3
	JMP sub_3op_no_carry_input

instr_cmn:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDA RDPTR
	STA RNPTR
	LDA #TEMP12 + 0
	STA RDPTR
	JMP add_3op_no_carry_input

instr_cmp2:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDA RDPTR
	STA RNPTR
	LDA #TEMP12 + 0
	STA RDPTR
	JMP sub_3op_no_carry_input

instrs_hireg_add_cmp:		;special data processing (1st half) ADD(4) CMP(3)
	LDA INSTR + 1
	ROR
	BCS instr_cmp3

instr_add4:
	JSR get_rm_hireg_3
	JSR get_rd_hireg_0
	LDA RDPTR
	STA RNPTR
	LDX #RNPTR + 0
	JSR check_p_x_points_to_pc_and_reloc_for_read
	LDX #RMPTR + 0
	JSR check_p_x_points_to_pc_and_reloc_for_read

	;now 3-op add with no flags
	LDY #0
	LDX #4
	CLC
add_loop_no_flags:
	LDA (RNPTR), Y
	ADC (RMPTR), Y
	STA (RDPTR), Y
	INY
	DEX
	BNE add_loop_no_flags
	JMP (INTERP)

instr_cmp3:
	JSR get_rm_hireg_3
	JSR get_rd_hireg_0
	LDA RDPTR
	STA RNPTR
	LDA #TEMP12 + 4
	STA RDPTR
	LDX #RNPTR + 0
	JSR check_p_x_points_to_pc_and_reloc_for_read
	LDX #RMPTR + 0
	JSR check_p_x_points_to_pc_and_reloc_for_read
	JMP sub_3op_no_carry_input

instrs_hireg_mov_bx_blx:
	LDA INSTR + 1
	ROR
	BCS instrs_bx_blx

instr_mov3:
	JSR get_rd_hireg_0
	JSR get_rm_hireg_3
	LDX #RMPTR + 0
	JSR check_p_x_points_to_pc_and_reloc_for_read
	LDX RMPTR
	LDY RDPTR

	LDA 0, X
	AND #$fe
	STA 0, Y
	LDA 1, X
	STA 1, Y
	LDA 2, X
	STA 2, Y
	LDA 3, X
	STA 3, Y
	JMP (INTERP)

instrs_bx_blx:
	JSR get_rm_hireg_3			;Rm cannot be PC so no need to check
	LDX RMPTR					;we need to read out Rm before we write LR (for BLX) since Rm can be LR
	LDA 0, X
	AND #$fe
	STA TEMP12 + 0
	LDA 1, X
	STA TEMP12 + 1
	LDA 2, X
	STA TEMP12 + 2
	LDA 3, X
	STA TEMP12 + 3

	LDA INSTR + 1
	BPL instr_no_blx
	LDA #1
	JSR set_lr_from_pc

instr_no_blx:
	LDA TEMP12 + 0
	STA REGS + $3c
	LDA TEMP12 + 1
	STA REGS + $3d
	LDA TEMP12 + 2
	STA REGS + $3e
	LDA TEMP12 + 3
	STA REGS + $3f
	JMP (INTERP)

instr_str2:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #4
	JSR memWR
	JMP (INTERP)

instr_strh2:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #2
	JSR memWR
	JMP (INTERP)

instr_strb2:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #1
	JSR memWR
	JMP (INTERP)

instr_ldrsb:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #1
	JSR memRD
	LDX RDPTR
	LDA #0
	LDY 0, X
	BPL instr_ldrsb_got_val
	LDA #$ff
instr_ldrsb_got_val:
	STA 1, X
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_ldrsh:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #1
	JSR memRD
	LDX RDPTR
	LDA #0
	LDY 1, X
	BPL instr_ldrsh_got_val
	LDA #$ff
instr_ldrsh_got_val:
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_ldr2:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #4
	JSR memRD
	JMP (INTERP)

instr_ldrh2:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #2
	JSR memRD
	LDX RDPTR
	LDA #0
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_ldrb2:
	JSR get_two_regs_added_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #1
	JSR memRD
	LDX RDPTR
	LDA #0
	STA 1, X
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_str1:
	JSR get_immX_6
	ASL
	ASL
	JSR get_reg_plus_imm_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #4
	JSR memWR
	JMP (INTERP)

instr_strh1:
	JSR get_immX_6
	ASL
	JSR get_reg_plus_imm_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #2
	JSR memWR
	JMP (INTERP)

instr_strb1:
	JSR get_immX_6
	AND #$1f
	JSR get_reg_plus_imm_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #1
	JSR memWR
	JMP (INTERP)

instr_ldr1:
	JSR get_immX_6
	ASL
	ASL
	JSR get_reg_plus_imm_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #4
	JSR memRD
	JMP (INTERP)

instr_ldrh1:
	JSR get_immX_6
	AND #$1f
	ASL
	JSR get_reg_plus_imm_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #2
	JSR memRD
	LDX RDPTR
	LDA #0
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_ldrb1:
	JSR get_immX_6
	AND #$1f
	JSR get_reg_plus_imm_addr
	JSR get_rd_loreg_0
	LDX RDPTR
	LDY #1
	JSR memRD
	LDX RDPTR
	LDA #0
	STA 1, X
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_str3:
	JSR get_sp_plus_imm_addr
	JSR get_rd_loreg_8
	LDX RDPTR
	LDY #4
	JSR memWR
	JMP (INTERP)

instr_ldr4:
	JSR get_sp_plus_imm_addr
	JSR get_rd_loreg_8
	LDX RDPTR
	LDY #4
	JSR memRD
	JMP (INTERP)

instr_ldr3:
	JSR get_rd_loreg_8
	LDX #TEMP12 + 0
	JSR get_pc_plus_imm_addr
	LDX RDPTR
	LDY #4
	JSR memRD
	JMP (INTERP)

instr_add5:
	JSR get_rd_loreg_8
	LDX RDPTR
	JSR get_pc_plus_imm_addr
	JMP (INTERP)

instr_add6:
	JSR get_rd_loreg_8
	LDX RDPTR
	JSR calc_sp_plus_imm
	JMP (INTERP)

instr_sub4:
	LDA #$80
	STA TEMPA
	ASL INSTR + 0
	ASL INSTR + 0
	ROL TEMPA		;captures (imm7 * 4)'s top bit, sets carry

	LDA REGS + $34
	SBC INSTR + 0
	STA REGS + $34

	LDA REGS + $35
	SBC TEMPA
	STA REGS + $35

	LDA REGS + $36
	SBC #0
	STA REGS + $36

	LDA REGS + $37
	SBC #0
	STA REGS + $37
	JMP (INTERP)

instrs_add7_sub4_cbz:
	LDA INSTR + 1
	ROR
	BCS instr_cbz_i_0
	LDA INSTR + 0
	BMI instr_sub4
	;fallthrough

instr_add7:
	LDX #REGS + $34
	JSR calc_sp_plus_imm
	JMP (INTERP)

instrs_cbz_extends:
	LDA INSTR + 1
	ROR
	BCS instr_cbz_i_1

instrs_extends:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR
	LDY RMPTR
	BIT INSTR + 0
	BPL instrs_extends_signed
	BVC instr_uxth

instr_uxtb:
	LDA 0, Y
	STA 0, X
instr_uxtb_write_zeroes:
	LDA #0
	STA 1, X
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_uxth:
	LDA 0, Y
	STA 0, X
	LDA 1, Y
	STA 1, X
instr_uxth_write_zeroes:
	LDA #0
	STA 2, X
	STA 3, X
	JMP (INTERP)

instrs_extends_signed:
	BVC instr_sxth

instr_sxtb:
	LDA 0, Y
	STA 0, X
	BPL instr_uxtb_write_zeroes
	LDA #$ff
	STA 1, X
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_sxth:
	LDA 0, Y
	STA 0, X
	LDA 1, Y
	STA 1, X
	BPL instr_uxth_write_zeroes
	LDA #$ff
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_cbz_i_1:
	LDA #$42
	BNE cbz_common	;faster than jmp

instr_cbz_i_0:
	LDA #$02

cbz_common:
	LDY #0

cbz_cbnz_common:	;A is what to add to imm5 after shift, incl the 2 extra that our PC offset needs, Y = 0 for CBZ, 1 for CBNZ
	PHA
	JSR get_rd_loreg_0
	LDX RDPTR
	LDA 0, X
	BNE cbz_cbnz_common_nz
	LDA 1, X
	BNE cbz_cbnz_common_nz
	LDA 2, X
	BNE cbz_cbnz_common_nz
	LDA 3, X
	BNE cbz_cbnz_common_nz

cbz_cbnz_common_z:
	CPY #0
	BEQ cbz_cbnz_taken

cbz_cbnz_not_taken:
	PLA
	JMP (INTERP)

cbz_cbnz_common_nz:
	CPY #0
	BEQ cbz_cbnz_not_taken

cbz_cbnz_taken:
	PLA
	CLC
	ADC REGS + $3c
	STA REGS + $3c
	BCC cbz_cbnz_out
	INC REGS + $3d
	BNE cbz_cbnz_out
	INC REGS + $3e
	BNE cbz_cbnz_out
	INC REGS + $3f
cbz_cbnz_out:
	JMP (INTERP)

instrs_cbnz_udf:
	LDA INSTR + 1
	ROR
	BCS instr_cbnz_i_0
	JMP udf_at_pc_minus_2

instr_cbnz_i_0:
	LDA #$02
	LDY #1
	BNE cbz_cbnz_common

instr_cbnz_i_1:
	LDA #$42
	LDY #1
	BNE cbz_cbnz_common

instr_push:
	LDA INSTR + 1
	ROR
	LDX #REGS + $38
	JSR push_one_reg
	LDA #$7
	STA TEMPA

push_loop:
	LDA TEMPA
	ASL
	ASL				;clears carry
	ADC #REGS
	TAX
	ROL INSTR + 0
	JSR push_one_reg
	DEC TEMPA
	BPL push_loop
	JMP (INTERP)

instr_pop:
	LDA #0
	STA TEMPA

pop_loop:
	LDA TEMPA
	ASL
	ASL				;clears carry
	ADC #REGS
	TAX
	ROR INSTR + 0
	JSR pop_one_reg
	INC TEMPA
	LDA TEMPA
	CMP #8
	BNE pop_loop

	LDX #REGS + $3c
	ROR INSTR + 1
	JSR pop_one_reg
	LDA REGS + $3c
	AND #$fe			;clear lowest bit of pc
	STA REGS + $3c
	JMP (INTERP)

instrs_cbnz_reverses:
	LDA INSTR + 1
	ROR
	BCS instr_cbnz_i_1

instrs_rev:
	JSR get_rd_loreg_0
	JSR get_rm_loreg_3
	LDX RDPTR
	LDY RMPTR
	BIT INSTR + 0
	BPL instrs_rev32_rev16
	BVS instr_revsh
	JMP udf_at_pc_minus_2

instrs_rev32_rev16:
	BVS instr_rev16

instr_rev:
	LDA 0, Y	;this construct in case same reg is input and output
	PHA
	LDA 1, Y
	PHA
	LDA 2, Y
	STA 1, X
	LDA 3, Y
	STA 0, X
	PLA
	STA 2, X
	PLA
	STA 3, X
	JMP (INTERP)

instr_rev16:
	LDA 0, Y	;this construct in case same reg is input and output
	PHA
	LDA 2, Y
	PHA
	LDA 1, Y
	STA 0, X
	LDA 3, Y
	STA 2, X
	PLA
	STA 3, X
	PLA
	STA 1, X
	JMP (INTERP)

instr_revsh:
	LDA 0, Y
	PHA
	LDA 1, Y
	STA 0, X
	PLA
	STA 1, X
	BMI instr_revsh_sext
	LDA #0
	STA 2, X
	STA 3, X
	JMP (INTERP)

instr_revsh_sext:
	LDA #$ff
	STA 2, X
	STA 3, X

instr_done_local_124:		;just convenient
	JMP (INTERP)

instr_bkpt_and_hints:
	ROR INSTR + 1
	BCC instr_bkpt
	LDA INSTR + 0
	BMI instr_hypercall		;we take over sone hint space that is not used
	;fallthrough

instr_hints:
	JMP (INTERP)

instr_bkpt:
	LDX #IDX_HARD_FAULT
	JMP exc_at_pc_minus_2

instr_hypercall:			;r0 (lower 7 bits) = request type; r1...r3 are params;
							;	0 = clear pending irqs (u32 mask)
							;	1 = get RTC (1khz) -> u32
							;	2 = get ticks (Fosc) -> u64
	LDA REGS + 0
	AND #$7f
	ASL						;clears carry
	ADC #<dispatch_hyper
	STA TEMPA
	LDA #>dispatch_hyper
	STA TEMPB
	JMP (TEMPA)



hyper_0x00_clear_irqs:
	LDA REGS + $04
	EOR #$ff
	AND G_IRQS_PEND
	STA G_IRQS_PEND
	JSR irqs_recalc
	JMP (INTERP)

hyper_0x01_get_rtc:
	LDA RTC + 0
	STA REGS + $00
	LDA RTC + 1
	STA REGS + $01
	LDA RTC + 2
	STA REGS + $02
	LDA RTC + 3
	STA REGS + $03
	JMP (INTERP)

hyper_0x02_get_ticks:
	LDX $11
	LDA $10
	STA REGS + $00
	LDA TICK_HI + 0
	STA REGS + $02
	LDA TICK_HI + 1
	STA REGS + $02
	LDA TICK_HI + 2
	STA REGS + $04
	CPX $11
	BNE hyper_0x02_get_ticks
	STX REGS + $02
	LDA #0
	STA REGS + $05
	STA REGS + $06
	STA REGS + $07
	JMP (INTERP)

instr_stmia:
	JSR get_rd_loreg_8
	LDA #$ff
	STA TEMPA
stmia_loop:
	INC TEMPA
	CLC
	ROR INSTR + 0
	BCS stmia_do_store
	BNE stmia_loop
	JMP (INTERP)

stmia_do_store:
	LDX RDPTR

	LDA 0, X
	STA TEMP12 + 0
	LDA 1, X
	STA TEMP12 + 1
	LDA 2, X
	STA TEMP12 + 2
	LDA 3, X
	STA TEMP12 + 3

	LDA TEMPA
	ASL
	ASL	;clears carry
	ADC #REGS
	TAX
	LDY #4
	JSR memWR

	LDX RDPTR
	CLC
	LDA 0, X
	ADC #4
	STA 0, X
	BCC stmia_loop
	INC 1, X
	BCC stmia_loop
	INC 2, X
	BCC stmia_loop
	INC 3, X
	JMP stmia_loop

instr_ldmia:			;no writeback if we load the reg, so we writeback before loading
	JSR get_rd_loreg_8

	;count number of loads
	LDA INSTR + 0
	TAY
	LDA popcount_tab, Y
	ASL
	ASL			;clears carry
	STA TEMPA	;uder 256
	LDX RDPTR
	LDA 0, X
	STA TEMP12 + 4
	ADC TEMPA
	STA 0, X
	LDA 1, X
	STA TEMP12 + 5
	ADC #0
	STA 1, X
	LDA 2, X
	STA TEMP12 + 6
	ADC #0
	STA 2, X
	LDA 3, X
	STA TEMP12 + 7
	ADC #0
	STA 3, X
	;u32@(TMP8 + 4) now has addr we'll use and increment, base reg has been writen-back to and we no longer care about its reg index

	LDA #$ff
	STA TEMPA
ldmia_loop:
	INC TEMPA
	CLC
	ROR INSTR + 0
	BCS ldmia_do_load
	BNE ldmia_loop
	JMP (INTERP)

ldmia_do_load:
	CLC
	LDA TEMP12 + 4
	STA TEMP12 + 0
	ADC #4
	STA TEMP12 + 4

	LDA TEMP12 + 5
	STA TEMP12 + 1
	ADC #0
	STA TEMP12 + 5

	LDA TEMP12 + 6
	STA TEMP12 + 2
	ADC #0
	STA TEMP12 + 6

	LDA TEMP12 + 7
	STA TEMP12 + 3
	ADC #0
	STA TEMP12 + 7

	LDA TEMPA
	ASL
	ASL	;clears carry
	ADC #REGS
	TAX
	LDY #4
	JSR memRD
	JMP ldmia_loop





push_one_reg:			;if C is clear, no push is done, else push reg pointed to by u32@MEM[X] to stack
	BCC push_pop_one_reg_done
	;fallthrough

push_one_reg_always:	;push reg pointed to by u32@MEM[X] to stack
	SEC
	LDA REGS + $34
	SBC #4
	STA REGS + $34
	STA TEMP12 + 0

	LDA REGS + $35
	SBC #0
	STA REGS + $35
	STA TEMP12 + 1

	LDA REGS + $36
	SBC #0
	STA REGS + $36
	STA TEMP12 + 2

	LDA REGS + $37
	SBC #0
	STA REGS + $37
	STA TEMP12 + 3
	
	LDY #4
	JMP memWR

push_pop_one_reg_done:
	RTS

pop_one_reg:			;if C is clear, no pop is done, else pop reg pointed to by u32@MEM[X] from stack
	BCC push_pop_one_reg_done

pop_one_reg_always:		;pop reg pointed to by u32@MEM[X] from stack

	CLC
	LDA REGS + $34
	STA TEMP12 + 0
	ADC #4
	STA REGS + $34

	LDA REGS + $35
	STA TEMP12 + 1
	ADC #0
	STA REGS + $35

	LDA REGS + $36
	STA TEMP12 + 2
	ADC #0
	STA REGS + $36

	LDA REGS + $37
	STA TEMP12 + 3
	ADC #0
	STA REGS + $37

	LDY #4
	JMP memRD

instrs_bhi_bls:
	ROR INSTR + 1
	BCS instr_bls

instr_bhi:
	LDA FLAG_Z
	BEQ instr_bcc_not_taken
	LDA FLAG_C
	ROR
	BCS instr_bcc_taken
instr_bcc_not_taken:
	JMP (INTERP)

instr_bls:
	LDA FLAG_Z
	BEQ instr_bcc_taken
	LDA FLAG_C
	ROR
	BCC instr_bcc_taken
	JMP (INTERP)

instrs_bge_blt:
	ROR INSTR + 1
	BCS instr_blt

instr_bge:
	LDA FLAG_V
	BNE instr_bge_v_set

instr_bge_v_clr:
	LDA FLAG_N
	BPL instr_bcc_taken
	JMP (INTERP)

instr_bge_v_set:
	LDA FLAG_N
	BMI instr_bcc_taken
	JMP (INTERP)

instr_blt:
	LDA FLAG_V
	BNE instr_blt_v_set

instr_blt_v_clr:
	LDA FLAG_N
	BMI instr_bcc_taken
	JMP (INTERP)

instr_blt_v_set:
	LDA FLAG_N
	BPL instr_bcc_taken
	JMP (INTERP)

instrs_bgt_ble:
	ROR INSTR + 1
	BCS instr_ble

instr_bgt:
	LDA FLAG_Z
	BNE instr_bge
	JMP (INTERP)

instr_ble:
	LDA FLAG_Z
	BEQ instr_bcc_taken
	BNE instr_blt

instrs_bvs_bvc:
	ROR INSTR + 1
	BCS instr_bvc

instr_bvs:
	LDA FLAG_V
	BNE instr_bcc_taken
	JMP (INTERP)

instr_bvc:
	LDA FLAG_V
	BEQ instr_bcc_taken
	JMP (INTERP)

instr_bcc_taken:
	LDA INSTR + 0
	BPL instr_bcc_taken_positive

instr_bcc_taken_negative:
	EOR #$7f	;will leave top bit set
	ASL			;will set carry
	STA TEMPA
	LDA REGS + $3c
	SBC TEMPA
	STA REGS + $3c
	BCS instr_bcc_taken_done
	LDA REGS + $3d
	SBC #0
	STA REGS + $3d
	BCS instr_bcc_taken_done
	LDA REGS + $3e
	SBC #0
	STA REGS + $3e
	BCS instr_bcc_taken_done
	DEC REGS + $3f
	JMP (INTERP)

instr_bcc_taken_positive:
	CLC
	ADC #1
	ASL			;can carry at this point if input was max possible displacement (+127 halfwords)
	BCS instr_bcc_taken_positive_max

instr_bcc_taken_positive_not_max:	;carry is clear, A is offset
	ADC REGS + $3c
	STA REGS + $3c
	BCC instr_bcc_taken_done

instr_bcc_taken_positive_max:		;we need to add 256 to PC, this is how
	INC REGS + $3d
	BNE instr_bcc_taken_done
	INC REGS + $3e
	BNE instr_bcc_taken_done
	INC REGS + $3f

instr_bcc_taken_done:
	JMP (INTERP)



instrs_beq_bne:
	ROR INSTR + 1
	BCS instr_bne

instr_beq:
	LDA FLAG_Z
	BEQ instr_bcc_taken
	JMP (INTERP)

instr_bne:
	LDA FLAG_Z
	BNE instr_bcc_taken
	JMP (INTERP)

instrs_bcs_bcc:
	ROR INSTR + 1
	BCS instr_bcc

instr_bcs:
	LDA FLAG_C
	ROR
	BCS instr_bcc_taken
	JMP (INTERP)

instr_bcc:
	LDA FLAG_C
	ROR
	BCC instr_bcc_taken
	JMP (INTERP)

instrs_bmi_bpl:
	ROR INSTR + 1
	BCS instr_bpl

instr_bmi:
	LDA FLAG_N
	BMI instr_bcc_taken
	JMP (INTERP)

instr_bpl:
	LDA FLAG_N
	BPL instr_bcc_taken
	JMP (INTERP)


instr_b_fwd:	;we know bit 10 is clear
	INC INSTR + 0
	BNE instr_b_fwd_inced
	INC INSTR + 1
instr_b_fwd_inced:
	ASL INSTR + 0
	ROL INSTR + 1
	CLC
	LDA REGS + $3c
	ADC INSTR + 0
	STA REGS + $3c
	LDA INSTR + 1
	AND #$3f
	ADC REGS + $3d
	STA REGS + $3d
	BCC instr_b_fwd_done
	INC REGS + $3e
	BNE instr_b_fwd_done
	INC REGS + $3f
instr_b_fwd_done:
	JMP (INTERP)

instr_b_back:
	INC INSTR + 0
	BNE instr_b_back_inced
	INC INSTR + 1
instr_b_back_inced:
	ASL INSTR + 0
	ROL INSTR + 1

	CLC
	LDA REGS + $3c
	ADC INSTR + 0
	STA REGS + $3c
	LDA INSTR + 1
	ORA #$f0
	ADC REGS + $3d
	STA REGS + $3d
	BCS instr_b_back_done
	LDA REGS + $3e
	SBC #0
	STA REGS + $3e
	BCS instr_b_back_done
	DEC REGS + $3f
instr_b_back_done:
	JMP (INTERP)



instr_t32_1111000:		;B.W or BL or UDF
instr_t32_1111010:		;B.W or BL or UDF
instr_t32_1111001:		;B.W or BL or MOVW or MOVT or UDF
instr_t32_1111011:		;B.W or BL or MOVW or MOVT or UDF
	LDX #TEMP12 + 4
	JSR fetch_instr_halfword	;second half will be in u16@(TEMP12+4)
	LDA #$10
	BIT TEMP12 + 5
	BPL instr_maybe_wide_move
	BEQ j_inval_t32_after_second_read
	BVC do_long_branch
;save LR
	LDA #1
	JSR set_lr_from_pc
	;fallthrough

do_long_branch:
	ASL INSTR + 0
	ROL INSTR + 1
	ASL INSTR + 0
	ROL INSTR + 1
	ASL INSTR + 0
	ROL INSTR + 1
	ASL INSTR + 0
	ROL INSTR + 1

	ASL TEMP12 + 4
	LDA TEMP12 + 5
	ROL
	STA TEMPA
	AND #$0f
	ORA INSTR + 0
	STA TEMP12 + 5
	
	ROL TEMPA
	LDA #$80
	AND TEMPA
	STA TEMPB
	LDA #$20
	AND TEMPA
	ASL
	ORA TEMPB
	STA TEMPA

	LDX #$ff
	LDA #$3f
	AND INSTR + 1
	ORA TEMPA
	BIT INSTR + 1
	BVS long_branch_s_handled
long_branch_s_clear:
	EOR #$c0
	LDX #$00
long_branch_s_handled:
	STA INSTR + 0
	
	;now proper offset is in X:(INSTR + 0):(TEMP12 + 5):(TEMP12 + 4) and it includes all proper offsets to PC since PC has now been advanved properly
	;we just need to add it

	CLC
	LDA REGS + $3c
	ADC TEMP12 + 4
	STA REGS + $3c
	LDA REGS + $3d
	ADC TEMP12 + 5
	STA REGS + $3d
	LDA REGS + $3e
	ADC INSTR + 0
	STA REGS + $3e
	TXA
	ADC REGS + $3f
	STA REGS + $3f
	
	JMP (INTERP)
	

j_inval_t32_after_second_read:
	JMP udf_at_pc_minus_4

/*

11110 s imm10  = 10 (j1) 1 (j2) imm11 - B.W
11110 s imm10  = 11 (j1) 1 (j2) imm11 - BL

decode is same for both
	i1 = J1 ^ !s
	i2 = J2 ^ !s
	ofst = s:i1:i2:imm10:imm11:0
	

	s handling
		if s == 0, invert 1 and 2

*/







instr_maybe_wide_move:
	LDA INSTR + 1
	AND #$03
	CMP #$02
	BNE j_inval_t32_after_second_read
	LDA INSTR + 0
	AND #$70
	CMP #$40
	BNE j_inval_t32_after_second_read

instrs_movw_movt:
	JSR get_rd_hireg_t32_word2_8
	LDX RDPTR
	LDA INSTR + 0
	BMI instr_movt

instr_movw:
	LDA #0
	STA 2, X
	STA 3, X
	BEQ instrs_movw_movt_common

instr_movt:
	INX
	INX

instrs_movw_movt_common:	;X points to the 16 bits we need to fill
	LDA TEMP12 + 4
	STA 0, X
	LDA INSTR + 1
	ASL
	AND #$08
	STA TEMPB

	LDA INSTR + 0
	AND #$0f
	STA TEMPA
	LDA TEMP12 + 5
	AND #$70
	ASL
	ORA TEMPA
	ROL
	ROL
	ROL
	ROL
	ORA TEMPB
	STA 1, X
	JMP (INTERP)

j_udf16:
	JMP udf_at_pc_minus_2

instr_t32_1111101:		;UDIV or SDIV or UDF
	ROR INSTR + 1
	BCC j_udf16
	LDA INSTR + 0
	AND #$d0
	CMP #$90
	BNE j_udf16

	LDX #TEMP12 + 4
	JSR fetch_instr_halfword	;second half will be in u16@(TEMP12+4)
	LDA TEMP12 + 4
	AND TEMP12 + 5
	AND #$f0
	CMP #$f0
	BEQ instrs_div

div_by_zero:
	LDX #IDX_USAGE_FAULT
	JMP exc_at_pc_minus_2

instrs_div:
	JSR get_rd_hireg_t32_word2_8
	JSR get_rm_hireg_t32_word2_0
	JSR get_rn_hireg_t32_word1_0
	LDX RMPTR
	LDA 0, X
	BNE div_has_num
	LDA 1, X
	BNE div_has_num
	LDA 2, X
	BNE div_has_num
	LDA 3, X
	BEQ div_by_zero

div_has_num:
	LDA #0
	STA TEMPA		;bottom bit will tell us if result needs inversion

	LDA INSTR + 1
	AND #$20
	BNE div_initial_signedness_resolved

div_is_signed:
	JSR negate_value_if_neg
	LDX RNPTR
	JSR negate_value_if_neg
	
div_initial_signedness_resolved:
	;copy num to TEMP12 + 0, denom to TEMP12 + 4, set "retmask" (TEMP12 + 8) to 0x00000001, clear Rd
	LDY #3
div_copy_1:
	LDA (RNPTR), Y
	STA (TEMP12 + 0), Y
	LDA (RMPTR), Y
	STA (TEMP12 + 4), Y
	LDA #0
	STA (TEMP12 + 8), Y
	STA (RDPTR), Y
	DEY
	BPL div_copy_1
	LDA #1
	STA TEMP12 + 8
	
	;shift denom and retmask left till denom has top bit set
	LDA TEMP12 + 7
	BMI div_denom_initial_shift_done

div_denom_initial_shift_loop:
	ASL TEMP12 + 8
	ROL TEMP12 + 9
	ROL TEMP12 + 10
	ROL TEMP12 + 11
	ROL TEMP12 + 4
	ROL TEMP12 + 5
	ROL TEMP12 + 6
	ROL TEMP12 + 7
	BPL div_denom_initial_shift_loop

div_denom_initial_shift_done:
	LDX RDPTR

div_main_loop:
	SEC
	LDA TEMP12 + 0
	SBC TEMP12 + 4
	LDA TEMP12 + 1
	SBC TEMP12 + 5
	LDA TEMP12 + 2
	SBC TEMP12 + 6
	LDA TEMP12 + 3
	SBC TEMP12 + 7
	BCC div_continue

div_denom_fits:
	LDA TEMP12 + 0
	SBC TEMP12 + 4
	STA TEMP12 + 0
	LDA TEMP12 + 1
	SBC TEMP12 + 5
	STA TEMP12 + 1
	LDA TEMP12 + 2
	SBC TEMP12 + 6
	STA TEMP12 + 2
	LDA TEMP12 + 3
	SBC TEMP12 + 7
	STA TEMP12 + 3
	
	LDA TEMP12 + 8
	ORA 0, X
	STA 0, X

	LDA TEMP12 + 9
	ORA 1, X
	STA 1, X

	LDA TEMP12 + 10
	ORA 2, X
	STA 2, X

	LDA TEMP12 + 11
	ORA 3, X
	STA 3, X

div_continue:
	CLC
	ROR TEMP12 + 7
	ROR TEMP12 + 6
	ROR TEMP12 + 5
	ROR TEMP12 + 4
	ROR TEMP12 + 11
	ROR TEMP12 + 10
	ROR TEMP12 + 19
	ROR TEMP12 + 18
	BCC div_main_loop
	
	;div is now done, remainder is in u32@(TEMP12 + 0), quotient is in u32@(MEM[X]), but we may need to invert it
	LDA TEMPA
	ROR
	BCC div_no_invert_result
	LDX RDPTR
	JSR negate_value

div_no_invert_result:
	JMP (INTERP)





;;;;;;;;;;;;;;;;;;;;;;;;;;;;; helpers ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;XXX: catch this at write time...

ret_exc:		;called when PC is written with top byte == 0xff
	LDA REGS + $3d
	AND REGS + $3e
	AND REGS + $3f
	EOR #$ff
	BNE ret_exc_inval_pc
	LDA REGS + $3c
	AND #$f7
	CMP #$f1
	BNE ret_exc_inval_pc

	;pop regs
	LDX #REGS + $00		;R0
	JSR pop_one_reg_always
	LDX #REGS + $04		;R1
	JSR pop_one_reg_always
	LDX #REGS + $08		;R2
	JSR pop_one_reg_always
	LDX #REGS + $0c		;R3
	JSR pop_one_reg_always
	LDX #REGS + $30		;R12
	JSR pop_one_reg_always
	LDX #REGS + $38		;LR
	JSR pop_one_reg_always
	LDX #REGS + $3c		;PC
	JSR pop_one_reg_always
	LDA TEMP12 + 4				;grab SR
	JSR pop_one_reg_always
	
	;	lower byte is IPSR
	LDA TEMP12 + 4
	STA IPSR
	;	middle low byte: were irqs on?
	LDA TEMP12 + 5
	BEQ ret_irqs_handled
	STA IRQSON			;on exc return we'd never be turning irqs off, only on, A is nonzero right now
ret_irqs_handled:
	;	high byte has flags
	LDA TEMP12 + 7
	STA FLAG_N
	ASL
	ASL
	ASL
	STA FLAG_V	;the only flag left, C  is in C
	LDA #0
	ROL
	STA FLAG_C
	LDA TEMP12 + 7
	AND #$40
	EOR #$40
	STA FLAG_Z
	RTS

ret_exc_inval_pc:
	LDA #IDX_HARD_FAULT
	;fallthrough to take_exc

take_exc:		;A = vector INDEX (0..63), pushed value of PC is whatever is in pc now, this may be wrong, adjust as needed
	PHA

	;form pushed SR in TEMP12 + 4
	;	lower byte is IPSR
	LDA IPSR
	STA TEMP12 + 4
	;	middle low byte: were irqs on?
	LDA IRQSON
	STA TEMP12 + 5
	;	middle bytes : unused
	LDA #0
	STA TEMP12 + 5
	STA TEMP12 + 6
	;	high byte has flags
	LDA FLAG_N
	AND #$80
	LDX FLAG_Z
	BNE vec_no_z
	ORA #$40
vec_no_z:
	LDX FLAG_V
	BEQ vec_no_v
	ORA #$10
vec_no_v:
	ROR FLAG_C	;corrupts it but we do not care
	BCC vec_no_c
	ORA #$20
vec_no_c:
	STA TEMP12 + 6

	;push it and the rest
	LDX #TEMP12 + 4	;SR
	JSR push_one_reg_always
	LDX #REGS + $3c	;PC
	JSR push_one_reg_always
	LDX #REGS + $38	;LR
	JSR push_one_reg_always
	LDX #REGS + $30	;R12
	JSR push_one_reg_always
	LDA #REGS + $0c	;R3
	JSR push_one_reg_always
	LDA #REGS + $08	;R2
	JSR push_one_reg_always
	LDA #REGS + $04	;R1
	JSR push_one_reg_always
	LDA #REGS + $00	;R0
	JSR push_one_reg_always

	;calc return LR
	LDA #$ff
	STA REGS + $3b
	STA REGS + $3a
	STA REGS + $39
	LDX IPSR
	LDA #$f9
	BEQ vec_was_thread_mode
	LDA #$f1
vec_was_thread_mode:
	STA REGS + $38

	PLA
	PHA
	STA IPSR		;store to IPSR
	CMP #IDX_FIRST_IRQ
	BCC vec_not_irq
	LDA #0
	STA IRQSON
vec_not_irq:
	PLA
	ASL
	ASL
	TAY

	;fallthrough

vector_read_to_pc:	;Y = vector offset (in bytes), clears lowest bit
	LDX #REGS + $3c
	LDA #$fe
	;fallthrough

vector_read_ex:	;A = value to AND with lowest byte; X = destination in our VA, Y = vector offset (in bytes) thus we support at most 16 external irqs, so sue me. 
	PHA
	TXA
	PHA

			TYA
			PHA
			LDA #$c2
			JSR uartChar
			PLA
			TAY
			PLA
			PHA
			TAX




	STY TEMP12 + 0
	LDA VTOR + 1
	STA TEMP12 + 1
	LDA VTOR + 2
	STA TEMP12 + 2
	LDA VTOR + 3
	STA TEMP12 + 3
	LDY #4
	JSR memRD

			LDA #$c1
			JSR uartChar


	PLA
	TAX
	PLA
	AND 0, X
	STA 0, X
	RTS

udf_at_pc_minus_4:					;does not return, calls next instr handler directly
	LDA #$fc
	BNE udf_at_pc_minus_A

exc_at_pc_minus_2:					;does not return, calls next instr handler directly
	LDA #$fe
	BNE exc_at_pc_minus_A

udf_at_pc_minus_2:					;does not return, calls next instr handler directly
	LDA #$fe

udf_at_pc_minus_A:					;does not return, calls next instr handler directly
	LDX #IDX_HARD_FAULT
	;fallthrough

exc_at_pc_minus_A:					;does not return, calls next instr handler directly
	CLC
	ADC REGS + $3c
	STA REGS + $3c
	LDA REGS + $3d
	SBC #$ff
	STA REGS + $3d
	LDA REGS + $3e
	SBC #$ff
	STA REGS + $3e
	LDA REGS + $3f
	SBC #$ff
	STA REGS + $3f

	TXA
	JSR take_exc
	JMP (TEMPA)

negate_value_if_neg:					;if (val < 0) { u32@MEM[X] *= -1; TEMPA++};
	LDA 3, X
	BPL negate_value_if_neg_done
	INC TEMPA

negate_value:							;u32@MEM[X] *= -1
	SEC
	LDA #0
	SBC 0, X
	STA 0, X
	LDA #0
	SBC 1, X
	STA 1, X
	LDA #0
	SBC 2, X
	STA 2, X
	LDA #0
	SBC 3, X
	STA 3, X
negate_value_if_neg_done:
	RTS

get_pc_plus_imm_addr:			;u32@MEM[X] = PC_as_Read_By_arm + 4 * imm8
	LDA #0
	STA TEMPA
	LDA INSTR + 0
	ASL
	ROL TEMPA

	ASL
	ROL TEMPA				;clears carry

get_pc_plus_arbitrary_imm:	;u32@MEM[X] = PC_as_Read_By_arm + (TEMPA : A), needs carry cleared
	ADC #2					;guaranteed to NOT carry
	ADC REGS + $3c			;may carry
	AND #$fc
	STA 0, X

	LDA TEMPA				;uses our carry properly
	ADC REGS + $3d
	STA 1, X

	LDA REGS + $3e
	ADC #0
	STA 2, X

	LDA REGS + $3f
	ADC #0
	STA 3, X
	RTS

get_sp_plus_imm_addr:			;u32@(TEMP12 + 0) = SP + 4 * imm8
	LDX #TEMP12 + 0

calc_sp_plus_imm:				;u32@MEM[X] = SP + 4 * imm8
	LDA #0
	STA TEMPA
	LDA INSTR + 0
	ASL
	ROL TEMPA
	ASL
	ROL TEMPA		;clears carry
	ADC REGS + $34
	STA 0, X

	LDA TEMPA
	ADC REGS + $35
	STA 1, X

	LDA REGS + $36
	ADC #0
	STA 2, X

	LDA REGS + $37
	ADC #0
	STA 3, X
	RTS

get_reg_plus_imm_addr:			;u32@(TEMP12 + 0) = Rn@3 + A
	PHA
	JSR get_rn_loreg_3
	PLA
	LDX RNPTR
	CLC
	ADC 0, X
	STA TEMP12 + 0
	
	LDA 1, X
	ADC #0
	STA TEMP12 + 1
	
	LDA 2, X
	ADC #0
	STA TEMP12 + 2
	
	LDA 3, X
	ADC #0
	STA TEMP12 + 3
	RTS

get_two_regs_added_addr:		;u32@(TEMP12 + 0) = Rn@3 + Rm@6
	JSR get_rm_loreg_6
	JSR get_rn_loreg_3
	LDX RNPTR
	LDX RMPTR
	CLC
	LDA 0, X
	ADC 0, Y
	STA TEMP12 + 0
	
	LDA 1, X
	ADC 1, Y
	STA TEMP12 + 1
	
	LDA 2, X
	ADC 2, Y
	STA TEMP12 + 2
	
	LDA 3, X
	ADC 3, Y
	STA TEMP12 + 3
	RTS


set_lr_from_pc:					;A = val to add to PC for saving to LR. recall that *OUR* pc points to first unread byte pf instrs
	CLC
	ADC REGS + $3c
	STA REGS + $38
	LDA #0
	ADC REGS + $3d
	STA REGS + $39
	LDA #0
	ADC REGS + $3e
	STA REGS + $3a
	LDA #0
	ADC REGS + $3f
	STA REGS + $3b
	RTS


check_p_x_points_to_pc_and_reloc_for_read:		;x points to pointer to maybe pc, assumes cur instr is 2 byte slong and thus PC is advanced by 2
	LDA 0, X			;Y now has actual pointer value
	CMP #$3c + REGS
	BNE check_p_x_points_to_pc_and_reloc_for_read_done
	CLC
	LDA REGS + $3c
	ADC #2
	STA TEMP12 + 0
	LDA REGS + $3d
	ADC #0
	STA TEMP12 + 1
	LDA REGS + $3e
	ADC #0
	STA TEMP12 + 2
	LDA REGS + $3f
	ADC #0
	STA TEMP12 + 3
	LDA #TEMP12 + 0
	STA 0, X
check_p_x_points_to_pc_and_reloc_for_read_done:
	RTS

copy_rm_to_rd:
	LDX RDPTR + 0
	CPX RMPTR + 0
	BEQ copy_rm_to_rd_done
	LDY RMPTR + 0
	LDA 0, Y
	STA 0, X
	LDA 1, Y
	STA 1, X
	LDA 2, Y
	STA 2, X
	LDA 3, Y
	STA 3, X
copy_rm_to_rd_done:
	RTS

get_rn_hireg_t32_word1_0:
	LDA INSTR + 0
	AND #$0f
	ASL
	ASL
	ORA #REGS
	STA RNPTR + 0
	RTS

get_rm_hireg_t32_word2_0:
	LDA TEMP12 + 4
	AND #$0f
	ASL
	ASL
	ORA #REGS
	STA RMPTR + 0
	RTS

get_rd_hireg_t32_word2_8:
	LDA TEMP12 + 5
	AND #$0f
	ASL
	ASL
	ORA #REGS
	STA RDPTR + 0
	RTS

get_rd_loreg_0:
	LDA INSTR + 0
	ROL
	ROL
	AND #$1c
	ORA #REGS
	STA RDPTR + 0
	RTS

get_rd_hireg_0:
	LDA INSTR + 0
	ROL
	ROL
	AND #$1c
	ORA #REGS
	STA RDPTR + 0
	LDA INSTR + 0
	ROR
	ROR
	AND #$20
	ORA RDPTR + 0
	STA RDPTR + 0
	RTS

get_rd_loreg_8:
	LDA INSTR + 1
	ROL
	ROL
	AND #$1c
	ORA #REGS
	STA RDPTR + 0
	RTS

get_rm_hireg_3:
	LDA INSTR + 0
	ROR
	AND #$3c
	CMP #$3c
	ORA #REGS
	STA RMPTR + 0
	RTS

get_rm_loreg_3:
	LDA INSTR + 0
	ROR
	AND #$1c
	ORA #REGS
	STA RMPTR + 0
	RTS

get_rm_loreg_6:
	LDA INSTR + 1
	ROR
	LDA INSTR + 0
	ROR
	ROR
	ROR
	ROR
	AND #$1c
	ORA #REGS
	STA RMPTR + 0
	RTS

get_rn_loreg_3:
	LDA INSTR + 0
	ROR
	AND #$1c
	ORA #REGS
	STA RNPTR + 0
	RTS

get_immX_6:	;produces (u8)(instr >> 6) needs to be followed by an AND
	LDA INSTR + 0
	ROL	;bit 7 into C, bit 6 into N
	BMI get_immX_6_6_1
	LDA INSTR + 1
	ROL
	ASL
	RTS
get_immX_6_6_1:
	LDA INSTR + 1
	ROL
	SEC
	ROL
	RTS

irqs_recalc:
	LDA G_IRQS_PEND
	AND G_IRQS_ENA
	BEQ irqs_recalc_none
	LDA IRQSON
	BEQ irqs_recalc_none

irqs_recalc_some:
	PHP
	SEI
	LDA #>do_irq_handle
	STA INTERP + 1
	LDA #<do_irq_handle
	STA INTERP + 0
	PLP
	RTS

irqs_recalc_none:
	PHP
	SEI
	LDA #>do_next_instr
	STA INTERP + 1
	LDA #<do_next_instr
	STA INTERP + 0
	PLP
	RTS


fosc_meas_setup:
	LDA #0
	STA $0f
	STA $10
	STA $11
	LDA #$3c
	STA $0f
	RTS

fosc_meas_wait_count_ends:
	LDA $10
	CMP $10
	BNE fosc_meas_wait_count_ends
	RTS

fosc_meas_wait_count_starts:
	LDA $10
	CMP $10
	BEQ fosc_meas_wait_count_starts
	RTS




;;;;;;;;;;;;;;;;;;;;;;;;;;;;; memory access ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 0x80000000 .. 0x807fffff -> direct BEX access where lower 15 bits are mapped to address and next 8 are bank select
; 0x40000000 .. 0x4000ffff -> direct address space access
;
; memory access assuems no region crossings
;
; X = source/destination (in first 256 bytes of memory)
; Y = length (1..4)
; u32@(TEMP12 + 0) = addr (corrupted)


memCommon:						;X = destination (in first 256 bytes of memory), Y = length (1..4), u32@(TEMP12 + 0) = addr, assumes no region crossings, corrupted
	BIT TEMP12 + 3
	BPL memCommon_not_bex
	LDA TEMP12 + 1
	ASL
	PHA
	LDA TEMP12 + 2
	ROL
	STA $0
	PLA
	BMI memCommon_bex_no_need_top
memCommon_bex_need_top:
	SEC
	ROR
	STA TEMP12 + 1
	BNE memCommon_top_set	;faster than jmp

memCommon_bex_no_need_top:
	CLC
	ROR
	STA TEMP12 + 1

memCommon_top_set:
memCommon_direct:
	STY TEMP12 + 2
	LDY #0
	SEC
	RTS

memCommon_not_bex:
	BVS memCommon_direct
	CLC
	RTS

fetch_instr_halfword:			;X = destination (in first 256 bytes of memory)
	CLC
	LDA REGS + $3c
	STA TEMP12 + 0
	ADC #2
	STA REGS + $3c
	LDA REGS + $3d
	STA TEMP12 + 1
	ADC #0
	STA REGS + $3d
	LDA REGS + $3e
	STA TEMP12 + 2
	ADC #0
	STA REGS + $3e
	LDA REGS + $3f
	STA TEMP12 + 3
	ADC #0
	STA REGS + $3f
	LDY #2
	;fallthrough to memRD

memRD:							;X = destination (in first 256 bytes of memory), Y = length (1..4), u32@(TEMP12 + 0) = addr, assumes no region crossings, corrupted
	JSR memCommon
	BCC memRD_end				;failure

memRD_loop:
	LDA (TEMP12), Y
	STA 0, X
	INX
	INY
	DEC TEMP12 + 2
	BNE memRD_loop

memRD_end:
	RTS


memWR:							;X = source (in first 256 bytes of memory), Y = length (1..4), u32@(TEMP12 + 0) = addr, assumes no region crossings,  corrupted
	JSR memCommon
	BCC memWR_end				;failure

memWR_loop:
	LDA 0, X
	STA (TEMP12), Y
	INX
	INY
	DEC TEMP12 + 2
	BNE memWR_loop

memWR_end:
	RTS




;;;;;;;;;;;;;;;;;;;;; dispatch tables



.align $100

dispatch_main:
	;dispatch based on top 7 bits, each entry is 2 bytes, table is 256 bytes and alignment makes it faster
;00000
	.word instr_lsl1_imm
	.word instr_lsl1_imm
	.word instr_lsl1_imm
	.word instr_lsl1_imm
;00001
	.word instr_lsr1_imm
	.word instr_lsr1_imm
	.word instr_lsr1_imm
	.word instr_lsr1_imm
;00010
	.word instr_asr1_imm
	.word instr_asr1_imm
	.word instr_asr1_imm
	.word instr_asr1_imm
;00011
	.word instr_add3
	.word instr_sub3
	.word instr_add1
	.word instr_sub1
;00100
	.word instr_mov1
	.word instr_mov1
	.word instr_mov1
	.word instr_mov1
;00101
	.word instr_cmp1
	.word instr_cmp1
	.word instr_cmp1
	.word instr_cmp1
;00110
	.word instr_add2
	.word instr_add2
	.word instr_add2
	.word instr_add2
;00111
	.word instr_sub2
	.word instr_sub2
	.word instr_sub2
	.word instr_sub2
;01000
	.word instrs_dp_reg				;data processing register (1st half) AND EOR LSL(2) LSR(2) ASR(2) ADC SBC ROR
	.word instrs_dp_reg				;data processing register (2nd half) TST NEG CMP(2) CMN ORR MUL BIC MVN
	.word instrs_hireg_add_cmp		;special data processing (1st half) ADD(4) CMP(3)
	.word instrs_hireg_mov_bx_blx	;special data processing (2nd half) MOV(3) BX BLX(2)
;01001
	.word instr_ldr3
	.word instr_ldr3
	.word instr_ldr3
	.word instr_ldr3
;01010
	.word instr_str2
	.word instr_strh2
	.word instr_strb2
	.word instr_ldrsb
;01011
	.word instr_ldr2
	.word instr_ldrh2
	.word instr_ldrb2
	.word instr_ldrsh
;01100
	.word instr_str1
	.word instr_str1
	.word instr_str1
	.word instr_str1
;01101
	.word instr_ldr1
	.word instr_ldr1
	.word instr_ldr1
	.word instr_ldr1
;01110
	.word instr_strb1
	.word instr_strb1
	.word instr_strb1
	.word instr_strb1
;01111
	.word instr_ldrb1
	.word instr_ldrb1
	.word instr_ldrb1
	.word instr_ldrb1
;10000
	.word instr_strh1
	.word instr_strh1
	.word instr_strh1
	.word instr_strh1
;10001
	.word instr_ldrh1
	.word instr_ldrh1
	.word instr_ldrh1
	.word instr_ldrh1
;10010
	.word instr_str3
	.word instr_str3
	.word instr_str3
	.word instr_str3
;10011
	.word instr_ldr4
	.word instr_ldr4
	.word instr_ldr4
	.word instr_ldr4
;10100
	.word instr_add5
	.word instr_add5
	.word instr_add5
	.word instr_add5
;10101
	.word instr_add6
	.word instr_add6
	.word instr_add6
	.word instr_add6
;10110
	.word instrs_add7_sub4_cbz
	.word instrs_cbz_extends
	.word instr_push
	.word instr_cps
;10111
	.word instrs_cbnz_udf
	.word instrs_cbnz_reverses
	.word instr_pop
	.word instr_bkpt_and_hints		;BKPT and udf is just udf
;11000
	.word instr_stmia
	.word instr_stmia
	.word instr_stmia
	.word instr_stmia
;11001
	.word instr_ldmia
	.word instr_ldmia
	.word instr_ldmia
	.word instr_ldmia
;11010
	.word instrs_beq_bne
	.word instrs_bcs_bcc
	.word instrs_bmi_bpl
	.word instrs_bvs_bvc
;11011
	.word instrs_bhi_bls
	.word instrs_bge_blt
	.word instrs_bgt_ble
	.word instr_svc
;11100
	.word instr_b_fwd
	.word instr_b_fwd
	.word instr_b_back
	.word instr_b_back
;11101
	.word udf_at_pc_minus_2		;no BLX(1)
	.word udf_at_pc_minus_2		;no BLX(1)
	.word udf_at_pc_minus_2		;no BLX(1)
	.word udf_at_pc_minus_2		;no BLX(1)
;11110
	.word instr_t32_1111000			;B.W or BL or UDF
	.word instr_t32_1111001			;B.W or BL or MOVW or MOVT or UDF
	.word instr_t32_1111010			;B.W or BL or UDF
	.word instr_t32_1111011			;B.W or BL or MOVW or MOVT or UDF
;11111
	.word udf_at_pc_minus_2		;no valid T32 instrs here
	.word instr_t32_1111101			;UDIV or SDIV or UDF
	.word udf_at_pc_minus_2		;no valid T32 instrs here
	.word udf_at_pc_minus_2		;no valid T32 instrs here


popcount_tab:				;256-byte aligned
	.byte 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5
	.byte 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6
	.byte 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6
	.byte 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7
	.byte 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6
	.byte 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7
	.byte 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7
	.byte 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8

dispatch_dp_reg:				;256-byte aligned
	.word instr_and
	.word instr_eor
	.word instr_lsl2
	.word instr_lsr2
	.word instr_asr2
	.word instr_adc
	.word instr_sbc
	.word instr_ror
	.word instr_tst
	.word instr_neg
	.word instr_cmp2
	.word instr_cmn
	.word instr_orr
	.word instr_mul
	.word instr_bic
	.word instr_mvn


dispatch_hyper:
	.word hyper_0x00_clear_irqs
	.word hyper_0x01_get_rtc
	.word hyper_0x02_get_ticks

UART		= $ae			;used by uart

uartDebug:
	LDY #REGS + $3c
	JSR uartWord
	LDY #INSTR
	JMP uartHalfword

uartWord:				;Y is addr
	LDA (3), Y
	JSR uartChar
	LDA (2), Y
	JSR uartChar
	;falthrough

uartHalfword:			;Y is addr
	LDA (1), Y
	JSR uartChar
	LDA (0), Y
	JMP uartChar


uartChar:				;A contains char, clobbers X
	ASL
	STA UART
	LDA #3
	ROL
	STA UART + 1
	LDX #11
	LDA #0

uartLoop:
	ROR UART + 1
	ROR UART + 0
	ROR
	ROR
	ROR		;shifted bit is now 0x20
	STA $20				;in port $20
	DEX
	BNE uartLoop
	RTS



