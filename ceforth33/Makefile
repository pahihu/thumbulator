RM = rm -f
ARMGNU ?= arm-none-eabi

CC = $(ARMGNU)-gcc
OPT = -Os -fomit-frame-pointer # 2876 bytes
# OPT = -O1 -fomit-frame-pointer # 3544 bytes
# OPT = -O2 -fomit-frame-pointer # 3356 bytes
# OPT = -O3 -fomit-frame-pointer # 3836 bytes
CPU = -mcpu=cortex-m0
# CPU = -mthumb
CFLAGS =  -Wall $(CPU) $(OPT) -nostdlib -ffreestanding -nostdlib -nostartfiles

all : ceforth ceforth33.bin

SRC = ceforth33.c nolib.c
OBJ = vectors.o $(SRC:.c=.o)

ceforth.boot : ceforth33.c bootlib.c nolib.h
	cc -Wall -o $@ -DBOOT=1 ceforth33.c bootlib.c

ceforth : dict32le.h ceforth33.c bootlib.c nolib.h
	cc -Wall -DHOSTED=1 -o $@ ceforth33.c bootlib.c

run.hosted: ceforth
	$(RM) run.hosted.log
	./ceforth 2>&1 | tee $@

boot.out: ceforth.boot
	echo bye | ./ceforth.boot > boot.out

dict32le.h: boot.out hex2h.awk
	awk -f hex2h.awk boot.out > $@

vectors.o : vectors.s
	$(ARMGNU)-as vectors.s -o vectors.o

ceforth33.s : ceforth33.c
	$(ARMGNU)-gcc $(CFLAGS) -S -fverbose-asm ceforth33.c

ceforth33.elf : dict32le.h $(OBJ) memmap
	$(ARMGNU)-gcc $(CFLAGS) $(OBJ) -T memmap -o $@ -lgcc
	$(ARMGNU)-size $@

ceforth33.bin : ceforth33.elf
	$(ARMGNU)-objdump -D $< > ceforth33.list
	$(ARMGNU)-objdump -t $< > ceforth33.map
	awk -f symbols.awk ceforth33.map > ceforth33.sym
	$(ARMGNU)-objcopy $< $@ -O binary
	ls -l $@

run: ceforth33.bin
	$(RM) run.log
	../thumbulator -m ceforth33.sym $< 2>&1 | tee run.log

clean:
	$(RM) *.bin *.o *.elf *.list *.map *.sym
	$(RM) ceforth.boot ceforth dict32le.h boot.out
	$(RM) *.log
	$(RM) -rf *.dSYM
	$(RM) ceforth33.s

# vim:noet :
