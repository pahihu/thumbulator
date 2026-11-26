#include <setjmp.h>
#include <stdlib.h>
static jmp_buf escape;
static int aborted;
static void stop_guest(int status) {
    aborted = status;
    longjmp(escape, 1);
}
#define main thumbulator_main
#define exit stop_guest
#include EMULATOR
#undef exit
#undef main

enum { ADDRESS = 0x400 };
static uint32_t sample(unsigned int variant)
{
    for (unsigned int i = 0; i < 13; ++i) {
        reg_norm[i] = variant == 0 ? i * 3 :
                      variant == 1 ? (i % 2 ? 0x40000400u : 0x40u) :
                                     (i % 2 ? 0x40u : 0x40000400u);
    }
    reg_norm[13] = 0x40001000;
    reg_norm[14] = 0x501;
    reg_norm[15] = ADDRESS + 2;
    cpsr = variant == 0 ? 0 : variant == 1 ? 0xF0000000 : 0x90000000;
    active_exception = 0;
    waiting = NoWait;
    reg_event = variant == 1;
    systick_pending = 0;
    systick_ctrl = systick_count = systick_reload = 0;
    control = primask = 0;
    psp = 0x40001800;
    shpr2 = shpr3 = 0x80800000;
    clks = fetches = reads = writes = 0;
    // Force the unrolled runner to yield after the one instruction under test.
    instructions = 127;
    memset(ram, 0xa5, 0x3000);

    int result;
    if (setjmp(escape) == 0)
#ifdef PAGED
        result = execute_page();
#else
        result = execute();
#endif
    else
        result = aborted + 40;

    uint32_t digest = result;
#define HASH(x) digest = digest * 16777619u ^ (x)
    for (unsigned int i = 0; i < 16; ++i) {
        HASH(reg_norm[i]);
    }
    HASH(cpsr);
    HASH(active_exception);
    HASH(waiting);
    HASH(reg_event);
    HASH(control);
    HASH(primask);
    HASH(psp);
    HASH(shpr2);
    HASH(shpr3);
    HASH(systick_ctrl);
    HASH(systick_reload);
    HASH(systick_count);
    HASH(clks);
    HASH(instructions);
    HASH(fetches);
    HASH(reads);
    HASH(writes);
    if (writes) {
        for (unsigned int i = 0; i < (0x3000 / 2); ++i) {
            HASH(ram[i]);
        }
    }
#undef HASH
    return digest;
}

static void check(unsigned int first, unsigned int second)
{
    rom[ADDRESS / 2] = first;
    rom[ADDRESS / 2 + 1] = second;
#ifdef PAGED
    // This harness rewrites ROM between cases. Within a case, keep the page
    // installed so the repeated samples also exercise predecoded execution.
    free(code_pages[ADDRESS / CODE_PAGE_BYTES]);
    code_pages[ADDRESS / CODE_PAGE_BYTES] = NULL;
#endif
    for (unsigned int variant = 0; variant < 3; variant++) {
        uint32_t first_result = sample(variant);
        uint32_t repeat_result = sample(variant);
        if (first_result != repeat_result) {
            printf("Inconsistent replay %04x %04x %u\n", first, second, variant);
            abort();
        }
        printf("%04x %04x %u %08x\n", first, second, variant, first_result);
    }
}

int main(void)
{
    // Vectors for UDF and SVC. Leave the other ROM words zeroed.
    rom[4 * EXCEPTION_HARDFAULT / 2] = 0x201;
    rom[4 * EXCEPTION_SVC / 2] = 0x301;
    // Keep emulator diagnostics off the output stream used for comparison.
    if (!freopen("/dev/null", "w", stderr)) return 1;
    for (unsigned int instr = 0; instr < 0xE800; ++instr) check(instr, 0);
    for (unsigned int instr = 0; instr < 0x10000; instr += 31) {
        check(0xF000 | (instr & 0x7FF), instr | 0xD000);
        check(0xF3EF, instr);
        check(0xF380 | (instr & 15), instr);
        check(0xF3BF, instr);
        check(0xF7F0 | (instr & 15), instr);
    }
    check(0xF3BF, 0x8F4F);
    check(0xF3BF, 0x8F5F);
    check(0xF3BF, 0x8F6F);
}
