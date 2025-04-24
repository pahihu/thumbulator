RM = rm -f
ARMGNU ?= arm-none-eabi

# COPS = -Wall -mthumb -O2 -fomit-frame-pointer -nostdlib -nostartfiles -ffreestanding
CC = $(ARMGNU)-gcc -fsigned-char
OPT = -O2
CFLAGS = -Wall -mthumb $(OPT) -nostdlib -ffreestanding #-DBOOT=1

all : ceforth33.bin

SRC = ceforth33.c nolib.c
OBJ = vectors.o $(SRC:.c=.o)

vectors.o : vectors.s
	$(ARMGNU)-as vectors.s -o vectors.o

ceforth33.bin : $(OBJ) memmap
	$(ARMGNU)-gcc $(CFLAGS) $(OBJ) -T memmap -o ceforth33.elf -lgcc
	$(ARMGNU)-objdump -D ceforth33.elf > ceforth33.list
	$(ARMGNU)-objdump -t ceforth33.elf > ceforth33.map
	awk -f symbols.awk ceforth33.map > ceforth33.sym
	$(ARMGNU)-objcopy ceforth33.elf ceforth33.bin -O binary
	ls -l ceforth33.bin

run: ceforth33.bin
	$(RM) run.log
	../thumbulator -s -m ceforth33.sym ceforth33.bin 2>run.log

clean:
	$(RM) *.bin *.o *.elf *.list *.map *.sym

# vim:noet :
