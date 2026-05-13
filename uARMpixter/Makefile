APP	= uARM
CC	= gcc
LD	= gcc


#PGO: first compile with --profile-generate, then run to completion, then compile with --profile-use

#fastmath breaks some NaN things in lh7xxxx_ADC.c
OPT			= -fomit-frame-pointer -momit-leaf-frame-pointer -Ofast -flto -fno-fast-math #--profile-use #--profile-generate
#OPT			= -O0 -ffunction-sections -Wl,--gc-sections


COMMON		= $(OPT) -g -ggdb -ggdb3 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable
CCFLAGS		= $(COMMON) -DGDB_STUB_ENABLED
LDFLAGS		= $(COMMON) -lSDL2

#main and soc
PROGRAM		+= main.o CPU.o mem.o RAM.o ROM.o gdbstub.o keys.o irqConnector.o
PROGRAM		+= lh7xxxx_IC.o lh7xxxx_RCPC.o lh7xxxx_CLCD.o lh7xxxx_Timer.o lh7xxxx_ADC.o
PROGRAM		+= lh7xxxx_IOCON.o lh7xxxx_GPIO.o lh7xxxx_UARTtypeB.o lh7xxxx_UARTtypeA.o
PROGRAM		+= lh7xxxx_UARTpolymorphism.o lh7xxxx_DMA.o

#MMU-based devices
WITH_MMU	+= cp15mmu.o MMU.o

#no-MPU no-MMU no-CP15 devices
NO_CP15		+= cp15none.o MXU.o


#LH754XX
LH754XX		+= $(NO_CP15) lh754xx_SMC.o lh754xx_SSP.o lh754xx_I2S.o

#LH795XX
LH795XX		+= $(WITH_MMU) lh795xx_SMC.o lh795xx_SSP.o lh795xx_I2S.o

#all ARM pixters
PIXTER		+= gpiodev_PixterBbBex.o gpiodev_MasterI2C.o i2cdev_24C02.o pixterMelodyChip.o pixterMelodyChipXL.o pixterRomFile.o at29lv010a.o

#pick a device
DEVICE		+= $(PIXTER) $(LH754XX) devicePixterColor.o
#DEVICE		+= $(PIXTER) $(LH795XX) devicePixterMultimedia.o mmiodev_PixterGpioExpander.o mmiodev_GPIONAND.o nand.o vBitbangedSPI.o i2s_spidev_TLV320DAC26.o



OBJS		= $(patsubst -D%,,$(DEVICE)) $(PROGRAM)
DFLAGS		= $(patsubst %.o,,$(DEVICE))

HFILES		= $(wildcard *.h)

$(APP): $(OBJS) $(HFILES)
	$(LD) -o $(APP) $(OBJS) $(LDFLAGS) $(DFLAGS)
	$(EXTRA)

%.o: %.c $(HFILES) Makefile
	$(CC) $(CCFLAGS) $(DFLAGS) -o $@ -c $<

clean:
	rm -f $(APP) $(OBJS)


