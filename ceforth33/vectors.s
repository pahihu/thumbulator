
/* vectors.s */
.cpu cortex-m3
.thumb

.word   0x40080000  /* stack top address */
.word   _start      /* 1 Reset */
.word   hang        /* 2 NMI */
.word   hang        /* 3 HardFault */
.word   hang        /* 4 MemManage */
.word   hang        /* 5 BusFault */
.word   hang        /* 6 UsageFault */
.word   hang        /* 7 RESERVED */
.word   hang        /* 8 RESERVED */
.word   hang        /* 9 RESERVED*/
.word   hang        /* 10 RESERVED */
.word   hang        /* 11 SVCall */
.word   hang        /* 12 Debug Monitor */
.word   hang        /* 13 RESERVED */
.word   hang        /* 14 PendSV */
.word   hang        /* 15 SysTick */
.word   hang        /* 16 External Interrupt(0) */
.word   hang        /* 17 External Interrupt(1) */
.word   hang        /* 18 External Interrupt(2) */
.word   hang        /* 19 ...   */


.thumb_func
hang:   b .

.thumb_func
.global _start
.extern _data_start
.extern _data_size
.extern _data_rom_start
.extern _bss_start
.extern _bss_size
_start:
    ldr r0,=_data_start         @ 4000_0000
    ldr r1,=_data_load_start    @ 0000_4c20
    ldr r2,=_data_size          @ 4000_010c
    bl memcpy
    ldr r0,=_bss_start          @ 4000_010c
    ldr r1,=0
    ldr r2,=_bss_size           @ 4001_0368
    bl memset
    bl notmain
    swi 1
    @ldr r0,=0xF0000000 ;@ halt
    @str r0,[r0]
    b .

.end
