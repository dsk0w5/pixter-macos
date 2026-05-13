LDFLAGS		= -g
CCFLAGS		= -fno-math-errno


ALLFLAGS	+= -Os -Wall -Wextra -Werror -marm -march=armv4t -I. -mfloat-abi=soft
ALLFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
ALLFLAGS	+= -DCPU_CLOCK_RATE=63000000 -flto -nostdlib

CCFLAGS	+= $(ALLFLAGS)
LDFLAGS += $(ALLFLAGS) -Wl,--gc-sections -Wl,-T $(LKR) -lm
CC		= arm-none-eabi-gcc
SOURCES += crt.S image.c main.c printf.c bbBus.c lz.c
LKR		= linker.lkr


APP		= pixterTest

#no changes below please

OBJS	= $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SOURCES)))

all: $(APP).bin $(APP).pci compress decompressTest

$(APP).elf: $(OBJS) $(LKR)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o : %.c
	$(CC) $(CCFLAGS) -c $< -o $@

%.o : %.S
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(APP).elf $(APP).bin $(APP).pci $(OBJS) compress decompressTest

image.o: image.inc bigimage.inc

bigimage.inc: big.bin compress
	./compress 0 < $< | xxd -i > $@

image.inc: image.wbm
	dd if=$< bs=2 skip=1 | xxd -i > $@

%.bin: %.elf
	arm-none-eabi-objcopy -O binary $< $@ -j.text -j.rodata -j.data -j.spacer
	truncate -s 16384 $@

%.pci: header.bin %.bin
	cat $+ >  $@

%:%.c lz.c
	gcc -Ofast -o $@ $^

