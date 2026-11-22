# Thumbulator memory-mapped I/O

This map describes the simulator built by the top-level Makefile. Firmware
should use aligned 32-bit loads and stores for these addresses, including the
byte-oriented console registers. Byte and halfword accesses go through the
halfword memory handlers and abort on peripheral addresses. Unlisted word reads
in the `0xE0000000` peripheral region abort. Unlisted word writes there are
ignored.

## Console and input events

The default console uses stdin for input and stdout for output.
`-p <port>` listens for a TCP connection and uses the accepted socket for both.
Input is nonblocking. The run loop checks host input every 128 executed
instructions, starting before the first instruction.

| Address | Access | Behavior |
| --- | --- | --- |
| `0xE0000000` | Write | Sends one byte to the host console. |
| `0xE0000004` | Read | Removes the oldest queued input byte. Returns zero if the queue is empty. |
| `0xE0000008` | Read | Returns `0xFFFFFFFF` when an input byte is queued, or zero otherwise. |
| `0xE000000C` | Read | Attempts a direct one-byte host read without consulting the queue. Returns zero if no byte is read. |
| `0xE0000010` | Read | Type of the queued event at the head of the queue, 0 for a key or console byte and 1 for an X11 mouse button. |
| `0xE0000014` | Read | X coordinate of the queued event. |
| `0xE0000018` | Read | Y coordinate of the queued event. |

The event registers return zero on an empty queue. Host console bytes have both
coordinates set to `-1`. An X11 mouse button queues the byte `128 + button`.
Check the input status to distinguish an empty queue from a queued zero byte.
Read the event type and coordinates before consuming the byte.

## ESP-AT UART and host clock

The independent ESP-AT UART is built into Thumbulator. Use 32-bit transfers
and check the applicable ready bit before accessing data.
The emulator supports outgoing TCP, one loopback TCP listener, and IPv4 UDP,
including peer metadata with `AT+CIPDINFO=1`. TCP connect and socket I/O are
nonblocking. TCP hostname resolution runs on a background thread.
The guest drives the modem by polling the UART registers.

| Address | Access | Behavior |
| --- | --- | --- |
| `0xE0000040` | Write | Sends the low byte to the ESP-AT emulator when TX is ready. |
| `0xE0000044` | Read | Consumes an ESP-AT response byte when RX is ready. |
| `0xE0000048` | Read | Bit 0 signals RX-ready. Bit 1 signals TX-ready. |
| `0xE000004C` | Read | Returns host Unix seconds as a 32-bit word. |

## SysTick and CPUID

| Address | Access | Behavior |
| --- | --- | --- |
| `0xE000E010` | Read/write | SysTick control and status. Bits 0, 1, 2 and 16 are ENABLE, TICKINT, CLKSOURCE and COUNTFLAG. All four bits are writable. A read returns the old value and clears COUNTFLAG. Setting ENABLE from 0 to 1 loads the current counter from RELOAD. |
| `0xE000E014` | Read/write | SysTick RELOAD, masked to 24 bits. |
| `0xE000E018` | Read/write | SysTick current counter. A write loads the supplied low 24 bits and clears COUNTFLAG. |
| `0xE000E01C` | Read/write | SysTick CALIB. At reset, NOREF is set, SKEW is set if the configured frequency is indivisible by 100, and TENMS is the frequency divided by 100 if the result fits in 24 bits. A write replaces the register with the supplied low 24 bits. |
| `0xE000ED00` | Read | CPUID. Defaults to zero. Set its hexadecimal value with `-c <cpuid>`. |

At reset, SysTick is disabled with CLKSOURCE set and RELOAD and the current
counter at zero. The counter uses elapsed simulated clocks between instructions.
CLKSOURCE is stored but does not select another clock. `-f <hz>` sets the
frequency used to compute CALIB, with a default of 8 MHz. It does not throttle
execution to host time. When ENABLE, TICKINT and COUNTFLAG are set outside a
handler, the simulator enters the handler using the vector at `0x0000003C`.
The writable COUNTFLAG and CALIB and the current-counter write behavior are
specific to this simulator.

## X11 framebuffer

Build with `HAVE_X11=1` defined for the C compiler and link with X11 to enable
`-g <width>x<height>`. The default build has X11 disabled. The allocated
framebuffer uses four bytes per pixel with a 15 MiB allocation limit.

| Address | Access | Behavior |
| --- | --- | --- |
| `0xE2000000`–`0xE2EFFFFF` | Read/write | Framebuffer aperture. The guest must enable the framebuffer and limit accesses to the allocated `width × height × 4` bytes. The simulator checks the aperture only. |
| `0xE2F00000` | Read/write | Framebuffer query selector. |
| `0xE2F00010` | Read | Returns the width for selector 1, the height for selector 2, or zero for any other selector. |

## Disk images

Each `-h <disk.img>` mounts a host file read/write in the next slot, starting at
0. Up to four images share one 512-byte transfer buffer. Transfers use byte
offsets and run synchronously when the command register is written.

| Address | Access | Behavior |
| --- | --- | --- |
| `0xE3000000` | Write | Low 32 bits of the byte offset. |
| `0xE3000008` | Write | High 32 bits of the byte offset. |
| `0xE3000010` | Write | Disk image slot, 0–3. |
| `0xE3000020` | Write | Starts a transfer. Zero reads from the image into the buffer. Any nonzero value writes the buffer to the image. |
| `0xE3000030` | Read | Status of the last transfer. 1 means all 512 bytes transferred. 0 means the request failed or the transfer was short. |
| `0xE3004000`–`0xE30041FF` | Read/write | Transfer buffer, 128 words. |

The last valid starting offset is `image_size - 512`. Transfers stay within the image.

## Debug output and halt

| Address | Access | Behavior |
| --- | --- | --- |
| `0xD0000000` | Write | Prints LR, the written address and the word value in hex to stderr. |
| `0xD0000010` | Write | Prints an eight-digit hexadecimal value with a `0x` prefix and a trailing space to stdout. |
| `0xD0000020` | Write | Prints the hexadecimal value with a trailing newline to stdout. |
| `0xF0000000`–`0xFFFFFFFF` | Write | Prints simulation counters and exits with status 0. The written value is ignored. |

Debug registers match the low address byte throughout the `0xD0000000` region.
Unmatched word writes in that region fall through to the RAM write path.
