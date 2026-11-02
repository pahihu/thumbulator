################
# The Cosmos
CC = cosmocc
OPTFLAGS = -O3 -fomit-frame-pointer
LDFLAGS =
################
# UN*X
# CC = gcc -flto
# OPTFLAGS = -O3 -ffunction-sections -fdata-sections
# LDFLAGS = -L /opt/X11/lib -lX11
# LDSTRIP=-Wl,--gc-sections
# STRIP=strip
CFLAGS = $(OPTFLAGS) -I /opt/X11/include -Wall
LDSTRIP=
STRIP=:
OS = $(shell uname)

# Linux
ifeq ($(OS),Linux)
	CFLAGS += -D_XOPEN_SOURCE=500
endif

# macOS
ifeq ($(OS),Darwin)
	LDSTRIP=-Wl,-dead_strip
	STRIP=strip
endif

thumbulator : thumbulator.o
	$(CC) $(LDSTRIP) -o thumbulator $(CFLAGS) thumbulator.o $(LDFLAGS)
	$(STRIP) -S -x thumbulator
	ls -l thumbulator

clean :
	rm -f thumbulator thumbulator.o
	rm -f output.vcd

# vim:set noet:




