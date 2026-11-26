.cpu cortex-m0
.syntax unified
.thumb
.text
.word 0x40080000
.word reset
.org 0x40
.thumb_func
reset:
    ldr r6, =0x40000000
    ldr r0, =0x47702001
    str r0, [r6]
    adds r5, r6, #1
    blx r5
    cmp r0, #1
    bne fail
    ldr r0, =0x47702002
    str r0, [r6]
    blx r5
    cmp r0, #2
    bne fail
    movs r0, #80
    b report
fail:
    movs r0, #70
report:
    ldr r4, =0xe0000000
    str r0, [r4]
    ldr r4, =0xf0000000
    str r0, [r4]
