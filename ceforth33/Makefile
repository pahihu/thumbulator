RM = rm -f
ARMGNU ?= arm-none-elf

# COPS = -Wall -mthumb -O2 -fomit-frame-pointer -nostdlib -nostartfiles -ffreestanding
CC = arm-none-elf-gcc -fsigned-char
CFLAGS = -Wall -mthumb -O2 -nostdlib -ffreestanding

all : ceforth33.bin

# vectors.o : vectors.s
# 	$(ARMGNU)-as vectors.s -o vectors.o

# putget.o : putget.s
# 	$(ARMGNU)-as putget.s -o putget.o

SRC = ceforth33.c nolib.c
OBJ = vectors.o putget.o $(SRC:.c=.o)

vectors.o : vectors.s
	$(ARMGNU)-as vectors.s -o vectors.o

putget.o : putget.s
	$(ARMGNU)-as putget.s -o putget.o

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
