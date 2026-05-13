.text
.section .text.vec
.thumb
.syntax unified

.globl start
start:
	ldr		r0, =0xfffe2000
	ldr		r1, [r0, #4]
	lsls	r1, r1, #16
	lsrs	r1, r1, #24
	cmp		r1, #0x52
	beq		is_multi
	bl		draw_on_color	//return true to not draw
	pop		{pc}

	.balign 4
is_multi:
	bx		pc			//go to arm mode
	nop
.arm
	MSR		CPSR, 0xD3	//irq and FIQ off
	ADR		SP,	start	//set up SP to be below this code

	//call our code, then go to where it ended up
	LDR		R12, =entry
	MOV		LR, PC		//returns here with address for us to jump to (likely start of PalmOS ROM)
	BX		R12

	//in hopes of clearing the data cache, read 256K of cacheable memory
	MOV		r3, #0x44000000
	ADD		r1, r3, 0x40000
readloop:
	LDR		r2, [r3], #32
	CMP		r3, r1
	BNE		readloop

	//disable cache, buffers, etc
	MRC		p15, 0, r1, c1, c0, 0
	BIC		r1, #0x0d	//turn off write buffer, cache, mmu
	MCR		p15, 0, r1, c1, c0, 0
	MRC		p15, 0, r1, c0, c0, 0
	NOP
	NOP
	SUB		PC, PC, #4
	
	//invalidate cache, TLB
	MOV		r1, #0
	MCR		p15, 0, r1, c8, c5, 0
	MCR		p15, 0, r1, c7, c7, 0

	BX		R0			//go to the ROM

//a cmall crt (not fast)

.arm
.globl strlen
strlen:
	movs	r1, r0
1:
	ldrb	r2, [r0]
	cmp		r2, #0
	beq		2f
	adds	r0, #1
	b		1b
2:
	subs	r0, r1, r0
	bx		lr

.globl memset
memset:
	mov		r12, r0
	subs	r2, #1
	bmi		1f
2:
	strb	r1, [r0, r2]
	subs	r2, #1
	bpl		2b
1:
	mov		r0, r12
	bx		lr


div_by_zero:
	b		div_by_zero

.globl __aeabi_uidiv
.globl __aeabi_uidivmod
__aeabi_uidivmod:
__aeabi_uidiv:
	movs	r2, #1
	movs	r3, r1
	beq		div_by_zero
	bmi		2f
1:
	lsls	r2, r2, #1
	lsls	r3, r3, #1
	bpl		1b
2:
	movs	r1, #0

3:
	cmp		r0, r3
	bcc		4f
	subs	r0, r3
	adds	r1, r2
4:
	lsrs	r3, r3, #1
	lsrs	r2, r2, #1
	bne		3b

	movs	r2, r0
	movs	r0, r1
	movs	r1, r2
	bx		lr











