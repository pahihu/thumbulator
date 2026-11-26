.cpu cortex-m0
.syntax unified
.thumb
.text
.word 0x40080000
.word reset
.org 0x40
.thumb_func
reset:
    movs r0, #0
    b cross_page
    udf #255                @ Predecode must not execute skipped instructions.

.org 0x7e
cross_page:
    bl target               @ Its second halfword is in the next 64/128-byte page.
    cmp r0, #3
    bne fail
    ldr r1, =indirect
    bx r1
fail:
    movs r0, #70
    b report
.ltorg

.org 0x140
.thumb_func
target:
    adds r0, #1
    cmp r0, #3
    bne target              @ Backward branch within the decoded page.
    bx lr                   @ Return to a different page.

.org 0x1c0
.thumb_func
indirect:
    movs r0, #80
report:
    ldr r4, =0xe0000000
    str r0, [r4]
    ldr r4, =0xf0000000
    str r0, [r4]
