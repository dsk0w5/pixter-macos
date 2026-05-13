APP			= uPixter

OPT			= -O0 -g -ggdb3
#OPT			= -Ofast
CC_FLAGS	=  -Wall -Wextra $(OPT) $(OPTS) -ffunction-sections -Wno-unused-variable -Wno-unused-function
LD_FLAGS	=  -Wall -Wextra $(OPT) -Wl,--gc-sections -lSDL2
CC			= gcc
LD			= gcc

#pisk one!!!!
CC_FLAGS	+= -DPIXTER_CLASSIC
#CC_FLAGS	+= -DPIXTER_PLUS
#CC_FLAGS	+= -DPIXTER_2_0

CC_FLAGS	+= -DUDP_IR			#only aplicable if emuating Pixter 2.0, very lazy impl :)
#CC_FLAGS	+= -DPREDICTABLE_TIME

OBJS		= 6502.o main.o rom.o melodyChip.o gpioMasterI2C.o i2cdev_24C02.o at29lv010a.o almostSST39SF010A.o

$(APP): $(OBJS)
	$(LD) -o $(APP) $(OBJS) $(LD_FLAGS) 

%.o : %.c Makefile
	$(CC) $(CC_FLAGS) -c $< -o $@

%.o : %.S
	$(CC) $(CC_FLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(APP)
