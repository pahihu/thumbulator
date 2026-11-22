This is a hybrid of the [original](https://github.com/dwelch67/thumbulator) and a [modified](https://github.com/ekoeppen/thumbulator) Thumbulator.
The goal is to run ceForth 3.3 on it. Work in progress.

Changes:
* rewrote ceforth33.c stacks using pointer arithmetic
* volatile UART regs, works in -O1/-O2 etc.
* ceForth V3.3 runs compiled in no opt. mode (-O0)

pahihu


This is a Thumb (16 bit ARM) instruction set simulator. Written
primarily for my experiments and for anyone wanting to learn the
instruction set but not having to read schematics or get many registers
set right to blink your first led or turn a pixel on on a display. I
started cleaning up the armulator from ARM (wanted to remove the
if-then-elses and target specific stuff and make it a clean, simple,
core) but got frustrated with it, I probably broke it so I wrote my own.

The single file thumbulator.c is the instruction set simulator (ISS), the
other directories are test programs and/or examples depending on how you
want to look at it.

It boots similar, well the same as a cortex-m where the exception vector
table is addresses to code instead of code like you have with an ARM
based core. The thumbulator does not support thumb2 instructions at
this time, if/when that happens it will likely be a separate simulator
to keep the two separate, simple, and clean.

The [memory-mapped I/O reference](doc/memory-mapped-io.md) covers the console,
input events, SysTick, CPUID, X11 framebuffer, disk images, debug output and halt.

Not necessarily cycle accurate to any real ARM core, it is designed
around a 16 bit data bus model similar to running on a GBA, and
instruction fetches and reads and writes to memory are counted and
displayed when you finish. One main reason for this program was to use
it to compare the output of various compilers.

No guarantees to be bug free, the tests I have thrown at it so far are
both special purpose and real world applications. And so far I have
cleaned up the instructions that didnt work, I have not checked code
coverage to see if every instruction has been hit. If you happen to
try it, and happen to find something please let me know. Beware though
the compilers and the code out there can be evil, I found an llvm bug
already, one or two of the examples I used mismanaged their integer
lengths. I used to like to use zlib as test code, but its integer
lengths are chaotic and it has become useless(to me)(as embedded or
test code) without a lot of rework.

d w e l c h a t d w e l c h d o t c o m

UPDATE:

The directories here are just a bunch of experiments and entertainments
for me, thumbulator.c is basically the project.

I have a bare_metal directory in my raspberrypi project you may find
interesting to understand how to put together a little bit of bootstrap
code to jump into a C program and go from there.  The blinker directory
(in this project) is perhaps a place to use as a beginning point on an
embedded program.

Google
  ARM DDI 0100E
or
  ARM DDI 0100I

https://www.scss.tcd.ie/~waldroj/3d1/arm_arm.pdf
http://morrow.ece.wisc.edu/ECE353/arm_reference/ddi0100e_arm_arm.pdf
http://reds.heig-vd.ch/share/cours/aro/ARM_Thumb_instructions.pdf
They have a rev B here...Which was the blue covered one in print.
http://www.home.marutan.net/arcemdocs/
https://www.intel.com/content/dam/www/programmable/us/en/pdfs/literature/third-party/archives/ddi0100e_arm_arm.pdf
https://www.cs.miami.edu/~burt/learning/Csc521.141/Documents/arm_arm.pdf



## Building with ESP-AT networking

Build the simulator on the host using the default Cosmopolitan compiler or a
native POSIX C compiler. The simulator builds from `thumbulator.c` with libc
and pthreads. Use `arm-none-eabi-gcc` to build the ARM firmware image.

```sh
make CC=cosmocc
make clean
make CC=cc
```

The ESP-AT protocol and socket transport are adapted from
[Oberon07-tools](https://github.com/pahihu/Oberon07-tools) at
`d15dc7b35148ce2d4ad57ced9589d95e218867ba`. The
[memory-mapped I/O reference](memory-mapped-io.md) lists the second UART
registers. The original console continues to use its own UART.
Run `python3 regress/esp_at.py ./thumbulator` to check TCP client, listener
and UDP traffic through a Cortex-M0 UART test ROM.
