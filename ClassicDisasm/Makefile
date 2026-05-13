APP			= disasm
SRCS		= main.c disasm.c util.c
CCFLAGS		= -Wall -Werror -Wno-unused-function -O0 -g -ggdb3
LDFLAGS		=


#do not touch
OBJS	= $(patsubst %.c,%.o,$(SRCS))
INCS	= $(wildcard *.h)


all: $(APP)

%.o: %.c $(INCS) Makefile
	gcc -o $@ -c $< $(CCFLAGS)

$(APP): $(OBJS) Makefile
	gcc -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS)