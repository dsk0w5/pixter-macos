ARMCC		= arm-none-eabi-gcc
ARMLD		= arm-none-eabi-gcc
ARMOBJCOPY	= arm-none-eabi-objcopy
ARMOPT		= -Os -mthumb -mcpu=cortex-m23
ARMLKR		= arm.lkr
ARMCCFLAGS	= $(ARMOPT) -mthumb -mcpu=cortex-m23 -I. -ffunction-sections -fdata-sections
ARMLDFLAGS	= $(ARMOPT) -Wl,--gc-sections -Wl,-T $(ARMLKR)
ARMBIN		= code.bina
ARMSRCS		= crt_m23.c main.c
ARMINCS		= 

BINARIES	= main.bin65 launcher.bin65
ROM			= rom

.SUFFIXES:

all: $(ROM).emu.pci $(ROM).real.bin

#arm things
ARMOBJS		= $(patsubst %.c,%.oa,$(ARMSRCS))


%.oa: %.c $(ARMINCS)
	$(ARMCC) -o $@ -c $< $(ARMCCFLAGS)

%.elfa: $(ARMOBJS)
	$(ARMLD) -o $@ $(ARMOBJS) $(ARMLDFLAGS)

%.bina: %.elfa
	$(ARMOBJCOPY) -I elf32-littlearm -O binary $< $@ -j.text -j.data -j.rodata -j.vectors





#6502 things


$(ROM).emu.pci: $(BINARIES) $(ARMBIN)
	printf 'PIXTER CLASSIC!\001' > $@	#header (16 bytes)
	printf "\000\000\000\000" >> $@		#0 melodies

	printf "\040\000\000\000" >> $@		#offset to code 0 (main)
	printf "\040\000\020\000" >> $@		#offset to code 1 (0x2000)
	printf "\040\040\020\000" >> $@		#total file size

	#code 0
	cat $(ARMBIN) >> $@				#arm code
	truncate -s 16160 $@			#cut to no more than proper size (0x3f00 + header so far)
	cat launcher.bin65 >> $@		#append header
	truncate -s 16416 $@			#truncate so that we've represented the first 0x4000 bytes of ROM so far (seen at 0x8000)
	yes " " | tr -d '\n' | cat | dd bs=16 count=1024 >> $@		#append 0x4000 of 0x20 to immitate open bus with pulls. this is cause our chip will not map 0xc000 to 0x4000 like emu will try, leaving the busopen
	truncate -s 1048608 $@			#truncate to full rom size (1 MB + header)
	
	#code 1
	cat main.bin65 >> $@			#grab main image
	truncate -s 1056800 $@			#truncate to full size (1 MB + 0x2000 + header)

$(ROM).real.bin: $(BINARIES) $(ARMBIN)
	cat $(ARMBIN) > $@				#arm image is first
	truncate -s 16128 $@
	cat launcher.bin65 >> $@		#pixter header is next header
	truncate -s 16384 $@			#truncate to size again
	truncate -s 1024000 $@			#truncate to where 0x2000 image goes
	cat main.bin65 >> $@			#grab main image (pixter classic emulator format has memory for 0x2000 up front)
	truncate -s 1048576 $@			#truncate to final size
	

%.o65: %.s $(INCS)
	ca65 -o $@ $<

%.bin65: %.o65 %.lkr
	ld65 -o $@ $< -C $*.lkr

clean:
	rm -f $(patsubst %.bin65,%.o,$(BINARIES)) $(BINARIES) $(ROM).emu.bin $(ROM).real.bin $(ARMBIN) $(ARMOBJS)