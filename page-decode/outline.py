from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
start = source.index('static void advance_systick(')
end = source.index('static unsigned int read_special', start)
source = source[:start] + '''// Reload arithmetic is reached only when a step reaches the end of a tick period.
#if defined(__GNUC__)
__attribute__((noinline, cold))
#endif
static void reload_systick(unsigned long ticks)
{
    const unsigned int period = systick_reload + 1;
    if (systick_count != 0 || (systick_reload != 0 && ticks >= period)) {
        systick_ctrl |= SYSTICK_COUNTFLAG;
        if (systick_ctrl & SYSTICK_TICKINT)
            systick_pending = 1;
    }
    systick_count = (period - (ticks - systick_count) % period) % period;
}

static void advance_systick(unsigned long ticks)
{
    if (!(systick_ctrl & SYSTICK_ENABLE) || ticks == 0)
        return;
    if (ticks < systick_count) {
        systick_count -= ticks;
        return;
    }
    reload_systick(ticks);
}

''' + source[end:]
start = source.index('static int interrupt_due(')
source = source[:start] + '''#if defined(__GNUC__)
__attribute__((noinline, cold))
#endif
static int pending_systick_can_run(void)
{
    return can_take_exception(EXCEPTION_SYSTICK);
}

''' + source[start:]
source = source.replace('return systick_pending && can_take_exception(EXCEPTION_SYSTICK);',
                        'return systick_pending && pending_systick_can_run();')
Path(sys.argv[2]).write_text(source)
