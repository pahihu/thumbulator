
thumbulator : thumbulator.c
	gcc -flto -ffunction-sections -fdata-sections -Wl,-dead_strip -o thumbulator -O3 thumbulator.c
	strip -S -x thumbulator
	ls -l thumbulator

clean :
	rm -f thumbulator
	rm -f output.vcd




