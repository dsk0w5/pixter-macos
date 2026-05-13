
 .feature c_comments

.segment "HEADER"
	.byte $AA, $55				; magic
	.byte 0, 0, 0, 0			; space
	.word __vm_entry			; vPC
	.byte 0, 0, 0, 0			; space 
	JMP __entry					; callout
	.byte $00					; version
	.byte $ff, $aa				; markers checked fo rby some games

__vm_entry:
	.byte $96

__entry:

UART		= $ae			;used by uart


	LDA #$c7
	STA $23
	LDA #$20
	STA $20

	LDA #$56
	JSR uartChar
	LDA #$f0
	JSR uartChar
	LDA #$cc
	JSR uartChar
	LDA #$33
	JSR uartChar

	JMP $2000


uartChar:				;A contains char
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

