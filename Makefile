CC = gcc -flto
OPTFLAGS = -O3
CFLAGS = $(OPTFLAGS) -I /opt/X11/include -Wall -D_XOPEN_SOURCE=500
LDFLAGS = -L /opt/X11/lib -lX11
# macOS: LDSTRIP=-Wl,-dead_strip
# Linux: LDSTRIP=-Wl,--gc-sections
LDSTRIP=
# STRIP=strip
STRIP=:

thumbulator : thumbulator.o
	$(CC) -ffunction-sections -fdata-sections $(LDSTRIP) -o thumbulator $(CFLAGS) thumbulator.o $(LDFLAGS)
	$(STRIP) -S -x thumbulator
	ls -l thumbulator

clean :
	rm -f thumbulator thumbulator.o
	rm -f output.vcd




