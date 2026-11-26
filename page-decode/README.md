# Page-decoded Thumbulator

`thumbulator.c` is a standalone version based on the split interpreter and [pahihu/thumbulator at f06a320](https://github.com/pahihu/thumbulator/commit/f06a320f05bc7493ac43dd680b4a40d53e4422d1).

This version defaults to 128-byte ROM pages (64 halfword slots). Define `CODE_PAGE_BYTES=64` for 32 slots. A cold page starts with every `func[]` entry pointing to `decode_current_page`. The first call decodes the entire page, installs each execution function and decoded operands, then executes the requested slot. Pages are allocated on demand and retained in a directory indexed by physical ROM page number. The 1,024-entry instruction cache is removed.

`execute_page()` contains a fixed call for each slot. Sequential instructions fall through to the next call. A taken jump within the page resumes at its target slot. For a jump outside the page, the same runner selects the ROM target page and calls its entry, which decodes the new page if cold. The runner yields for RAM execution, host polling, wait instructions, and interrupt handling. Fetch and SysTick accounting still run at instruction boundaries. The decoder reads a wide instruction's second halfword from the next ROM page when required. Instruction tracing and writable RAM use the single-instruction path.

## Cortex-M ComsTime

Medians of eight runs per build on a single logical CPU of an AMD EPYC 9V74, using GCC 14.2.0 with `-O3` and the same POCC v181 firmware at 12 MHz simulated frequency. Execution order was balanced and statistics were collected in separate runs.

| Build | Wall time | Process CPU time | Decoder storage on ComsTime | Host ELF .text |
| --- | ---: | ---: | ---: | ---: |
| Stock | 3.009 s | 2.999 s | 0 KiB | 43.44 KiB |
| Previous split interpreter, 1,024 entries | 2.595 s | 2.579 s | 16 KiB | 53.75 KiB |
| 64-byte pages | 1.891 s | 1.886 s | 197 KiB | 62.87 KiB |
| 128-byte pages | 1.894 s | 1.889 s | 139 KiB | 74.00 KiB |

The 128-byte build reduced median wall time by 27.0% versus the previous split interpreter and 37.1% versus stock. The two page sizes had similar timings. The 128-byte default uses less decoder storage on this workload, while the 64-byte build has a smaller host executable code section. Decoder storage includes the page directory and decoded pages, excluding allocator overhead. ComsTime decoded 138 pages at 64 bytes and 75 pages at 128 bytes. Both builds ran 172,566,394 instructions through the page runner and 54,820 through the single-instruction path.

Every timed run produced identical guest output and simulated counters. The page-boundary fixture tests a wide `BL` across both page sizes, a backward branch, a return to another page, an indirect cross-page jump, and a skipped `UDF`. The system-instruction and writable-RAM fixtures match stock with and without disassembly. The page-boundary fixture also produces identical VCD output. Single-step checks matched stock across 209,910 combinations of opcode and initial state, with cold decode and predecoded replay.

## Build and reproduce

```sh
git clone https://github.com/pahihu/thumbulator.git thumb-study
git -C thumb-study checkout f06a320f05bc7493ac43dd680b4a40d53e4422d1

gcc -O3 -D_XOPEN_SOURCE=500 -pthread thumb-study/thumbulator.c -o stock
gcc -O3 -D_XOPEN_SOURCE=500 -pthread thumbulator-split-1024.c -o split1024
gcc -O3 -D_XOPEN_SOURCE=500 -pthread -DCODE_PAGE_BYTES=64 thumbulator.c -o page64
gcc -O3 -D_XOPEN_SOURCE=500 -pthread thumbulator.c -o page128

python3 benchmark.py --runs 8 --build stock=stock --build split1024=split1024 \
    --build page64=page64 --build page128=page128 --output rerun.json
```

On Linux, use `taskset -c <cpu>` on the benchmark command to pin all runs. Build with `-DPAGE_STATS=1` to collect decoder storage and execution counts. The raw timings from the table are in `measurements.json`.

```sh
./page128 fixtures/page-boundary.bin </dev/null
./page128 fixtures/system.bin </dev/null
./page128 fixtures/ram.bin </dev/null

gcc -O3 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=500 -pthread \
    '-DEMULATOR="thumb-study/thumbulator.c"' check-step.c -o check-stock
gcc -O3 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=500 -pthread -DPAGED \
    '-DEMULATOR="thumbulator.c"' check-step.c -o check-page
./check-stock > stock-steps.txt
./check-page > page-steps.txt
cmp stock-steps.txt page-steps.txt
```
