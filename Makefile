CC = gcc -flto
OPTFLAGS = -O3
CFLAGS = $(OPTFLAGS) -I /opt/X11/include
LDFLAGS = -L /opt/X11/lib -lX11

thumbulator : thumbulator.o
	$(CC) -ffunction-sections -fdata-sections -Wl,-dead_strip -o thumbulator $(CFLAGS) thumbulator.o $(LDFLAGS)
	strip -S -x thumbulator
	ls -l thumbulator

clean :
	rm -f thumbulator
	rm -f output.vcd




