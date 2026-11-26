.cpu cortex-m0
.syntax unified
.thumb
.text
.global reset
.macro require_eq
    beq 1f
    b fail
1:
.endm
.word 0x40080000
.word reset
.word 0
.word hardfault
.space 28
.word svcall
.space 12
.word systick

.thumb_func
reset:
    movs r6, #0
    cpsid i
    mrs r0, primask
    cmp r0, #1
    require_eq
    movs r1, #0
    msr primask, r1
    mrs r0, primask
    cmp r0, #0
    require_eq
    ldr r1, =0xa8000000
    msr apsr_nzcvq, r1
    mrs r0, xpsr
    ldr r1, =0xa0000000
    cmp r0, r1
    require_eq
    mrs r0, epsr
    cmp r0, #0
    require_eq

    mrs r4, msp
    ldr r0, =0x40080000
    cmp r4, r0
    require_eq
    subs r4, #8
    mov r0, r4
    adds r0, #3
    msr msp, r0
    mov r0, sp
    cmp r0, r4
    require_eq
    svc #1
    cmp r0, #1
    require_eq

    ldr r5, =0x40070004
    mov r0, r5
    adds r0, #3
    msr psp, r0
    movs r0, #3
    msr control, r0
    isb
    mrs r0, control
    cmp r0, #2
    require_eq
    mov r0, sp
    cmp r0, r5
    require_eq
    svc #0xcc
    cmp r0, #0xcc
    require_eq
    mov r0, sp
    cmp r0, r5
    require_eq
    mrs r0, ipsr
    cmp r0, #0
    require_eq

    cpsid i
    svc #2
    mrs r0, primask
    cmp r0, #0
    require_eq
    cmp r6, #1
    require_eq
    ldr r0, =0xe000ed1c
    ldr r1, =0xffffffff
    str r1, [r0]
    ldr r1, [r0]
    ldr r0, =0xc0000000
    cmp r1, r0
    require_eq
    ldr r0, =0xe000ed1c
    movs r1, #0
    str r1, [r0]
    ldr r0, =0xe000ed20
    ldr r1, =0xc0000000
    str r1, [r0]
    ldr r7, =0xe000e010
    movs r1, #64
    str r1, [r7, #4]
    movs r1, #7
    str r1, [r7]
wait_tick:
    cmp r6, #2
    bne wait_tick
    mrs r0, ipsr
    cmp r0, #0
    require_eq
    movs r0, #80
    b report

.thumb_func
svcall:
    mrs r1, ipsr
    cmp r1, #11
    require_eq
    mrs r1, control
    cmp r1, #0
    require_eq
    mov r1, lr
    ldr r2, =0xfffffff1
    cmp r1, r2
    beq msp_frame
    ldr r2, =0xfffffff9
    cmp r1, r2
    beq msp_frame
    ldr r2, =0xfffffffd
    cmp r1, r2
    require_eq
    mov r1, sp
    cmp r1, r4
    require_eq
    mrs r0, psp
    b read_svc
msp_frame:
    mov r0, sp
read_svc:
    ldr r1, [r0, #24]
    subs r1, #2
    ldrb r1, [r1]
    str r1, [r0]
    bx lr

.thumb_func
systick:
    push {lr}
    movs r0, #0
    str r0, [r7]
    svc #3
    cmp r0, #3
    require_eq
    mrs r0, ipsr
    cmp r0, #15
    require_eq
    movs r6, #2
    pop {pc}

.thumb_func
hardfault:
    mrs r0, ipsr
    cmp r0, #3
    require_eq
    mrs r0, psp
    ldr r1, [r0, #24]
    subs r1, #2
    ldrh r1, [r1]
    ldr r2, =0xdf02
    cmp r1, r2
    require_eq
    cpsie i
    movs r6, #1
    bx lr

fail:
    movs r0, #70
report:
    ldr r1, =0xe0000000
    str r0, [r1]
    ldr r1, =0xf0000000
    str r0, [r1]
