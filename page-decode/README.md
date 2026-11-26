# Thumbulator page runner code-size experiment

`thumbulator.c` keeps the existing page runner, including 128-byte pages by default, and outlines the SysTick reload and pending-interrupt priority checks into shared, out-of-line helpers. The normal per-instruction countdown stays inline. Decoded ROM pages are still kept for reuse, and interrupts are still checked at guest instruction boundaries. The source is based on the prior page-decoded version.

`thumbulator-short-unroll.c` is an alternative using a smaller unroll factor over the same page. Define `PAGE_UNROLL=1`, `2`, `4`, `8`, or `16` to select it. This knob controls the number of duplicated dispatch sites. `CODE_PAGE_BYTES` still selects the size of the decoded page independently.

The included ComsTime binary was run at 12 MHz simulated frequency. Tests used GCC 14.2.0 with `-O3` on a single logical CPU of an AMD EPYC 9V74. The host reports a 32 KiB L1 instruction cache. Instruction-cache miss counters were unavailable.

## Reproduce

```sh
gcc -O3 -D_XOPEN_SOURCE=500 -pthread thumbulator-original.c -o original
gcc -O3 -D_XOPEN_SOURCE=500 -pthread thumbulator.c -o full-outline
for n in 1 4 8 16; do
    gcc -O3 -D_XOPEN_SOURCE=500 -pthread -DPAGE_UNROLL=$n thumbulator-short-unroll.c -o unroll-$n
done
python3 outline.py thumbulator-short-unroll.c short-outline.c
gcc -O3 -D_XOPEN_SOURCE=500 -pthread short-outline.c -o short-outline
python3 benchmark.py --runs 7 --build original=original --build unroll-1=unroll-1 \
    --build unroll-4=unroll-4 --build unroll-8=unroll-8 --build unroll-16=unroll-16 \
    --build full-outline=full-outline --build short-outline=short-outline --output rerun.json
```

Use `taskset -c <cpu>` to pin the benchmark on Linux. The benchmark verifies guest stdout, including simulated counters, on every run.

## Results

Median of seven runs per build with rotating execution order. Code size is the host ELF `.text` section.

| Runner | Wall time | Process CPU time | Host code |
| --- | ---: | ---: | ---: |
| Original, 64 dispatch sites | 1.840 s | 1.831 s | 74.0 KiB |
| 1 dispatch site | 1.770 s | 1.763 s | 51.3 KiB |
| 4 dispatch sites | 1.740 s | 1.734 s | 52.4 KiB |
| 8 dispatch sites | 1.820 s | 1.811 s | 54.1 KiB |
| 16 dispatch sites | 1.813 s | 1.806 s | 57.0 KiB |
| 64 dispatch sites, shared slow paths | 1.720 s | 1.714 s | 66.9 KiB |
| 4 dispatch sites, shared slow paths | 1.731 s | 1.722 s | 51.9 KiB |

The supplied primary version, with 64 dispatch sites and shared slow paths, took 6.5% less wall time than the original. The 4-site version with shared slow paths cut host code by 29.9% with a median 11 ms higher than the primary version. Profiles of hardware L1 misses were unavailable, so the timings establish the effect of the code changes under this workload.

ComsTime output and guest counters matched across all builds. The system, writable RAM, and page-boundary fixtures matched with and without disassembly. Both versions with shared slow paths matched 209,910 single-step cases and the page-boundary VCD output. The primary version also matched ComsTime with 64-byte decoded pages.
