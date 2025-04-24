RM = rm -f
ARMGNU ?= arm-none-eabi

# COPS = -Wall -mthumb -O2 -fomit-frame-pointer -nostdlib -nostartfiles -ffreestanding
CC = $(ARMGNU)-gcc
OPT = -O2
CFLAGS =  -Wall -mthumb -march=armv6-m $(OPT) -nostdlib -ffreestanding -nostdlib -nostartfiles

all : ceforth33.bin

SRC = ceforth33.c nolib.c
OBJ = vectors.o $(SRC:.c=.o)

ceforth.boot : ceforth33.c bootlib.c
	cc -o $@ -DBOOT=1 ceforth33.c bootlib.c

ceforth : dict32le.h ceforth33.c bootlib.c
	cc -DHOSTED=1 -o $@ ceforth33.c bootlib.c

dict32le.h: ceforth.boot
	echo bye | ./ceforth.boot > boot.out
	awk -f hex2h.awk boot.out > dict32le.h

vectors.o : vectors.s
	$(ARMGNU)-as vectors.s -o vectors.o

ceforth33.s : ceforth33.c
	$(ARMGNU)-gcc $(CFLAGS) -S -fverbose-asm ceforth33.c

ceforth33.bin : dict32le.h $(OBJ) memmap
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
	$(RM) ceforth.boot ceforth dict32le.h boot.out
	$(RM) *.log
	$(RM) -rf *.dSYM
	$(RM) ceforth33.s

# vim:noet :
