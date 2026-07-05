# Duel Minimax Parallelization Report

## Baseline

- Date: 2026-07-05
- HPC probe command: `bash codex-skills/hpc-parallel-c/scripts/hpc_env_probe.sh > /tmp/minimax-hpc-env.txt`
- Serial build: `python3 setup.py build_ext --inplace --force`
- Fixed-depth command: `python3 -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 1 --budgets 400 --fixed-depths 6,8 --runs 15 --warmup 3 --out exports/minimax_parallel/serial-fixed-depth.jsonl`
- Live-budget command: `python3 -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 1 --budgets 180,320,400 --fixed-depths 0 --runs 15 --warmup 3 --out exports/minimax_parallel/serial-budget.jsonl`
- Correctness oracle: fixed-depth serial and parallel modes must match move and score within absolute tolerance `1e-6`.
- Speedup formula: `speedup = serial_elapsed_ms_p50 / candidate_elapsed_ms_p50`.
- Positive-impact threshold: keep a mode only when speedup is at least `1.05` on at least four duel scenarios, with no correctness failures and live `/move` p95 at or below `450ms`.

Baseline artifacts:

These are local, uncommitted benchmark artifacts:

- `exports/minimax_parallel/serial-fixed-depth.jsonl`
- `exports/minimax_parallel/serial-budget.jsonl`

## Environment

```text
== system ==
Linux sergei-scv-lin 6.8.0-124-generic #124~22.04.1-Ubuntu SMP PREEMPT_DYNAMIC Tue May 26 21:05:19 UTC  x86_64 x86_64 x86_64 GNU/Linux
online_cpus=16

== lscpu ==
Architecture:                            x86_64
CPU op-mode(s):                          32-bit, 64-bit
Address sizes:                           39 bits physical, 48 bits virtual
Byte Order:                              Little Endian
CPU(s):                                  16
On-line CPU(s) list:                     0-15
Vendor ID:                               GenuineIntel
Model name:                              13th Gen Intel(R) Core(TM) i5-1340P
CPU family:                              6
Model:                                   186
Thread(s) per core:                      2
Core(s) per socket:                      12

== cc ==
cc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


== gcc ==
gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


== clang ==
Ubuntu clang version 14.0.0-1ubuntu1.1
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin

== mpicc ==
gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


== mpirun ==
HYDRA build details:
    Version:                                 4.0
    Release Date:                            Fri Jan 21 10:42:29 CST 2022
    CC:                              gcc -Wdate-time -D_FORTIFY_SOURCE=2 -g -O2 -ffile-prefix-map=/build/mpich-0xgrG5/mpich-4.0=. -flto=auto -ffat-lto-objects -flto=auto -ffat-lto-objects -fstack-protector-strong -Wformat -Werror=format-security  -Wl,-Bsymbolic-functions -flto=auto -ffat-lto-objects -flto=auto -Wl,-z,relro
    Configure options:                       '--with-hwloc-prefix=/usr' '--with-device=ch4:ofi' 'FFLAGS=-O2 -ffile-prefix-map=/build/mpich-0xgrG5/mpich-4.0=. -flto=auto -ffat-lto-objects -flto=auto -ffat-lto-objects -fstack-protector-strong -fallow-invalid-boz -fallow-argument-mismatch' '--prefix=/usr' 'CFLAGS=-g -O2 -ffile-prefix-map=/build/mpich-0xgrG5/mpich-4.0=. -flto=auto -ffat-lto-objects -flto=auto -ffat-lto-objects -fstack-protector-strong -Wformat -Werror=format-security' 'LDFLAGS=-Wl,-Bsymbolic-functions -flto=auto -ffat-lto-objects -flto=auto -Wl,-z,relro' 'CPPFLAGS=-Wdate-time -D_FORTIFY_SOURCE=2'
    Process Manager:                         pmi
    Launchers available:                     ssh rsh fork slurm ll lsf sge manual persist
    Topology libraries available:            hwloc
    Resource management kernels available:   user slurm ll lsf sge pbs cobalt
    Demux engines available:                 poll select

== perf ==
perf version 6.8.12

== nsys ==
missing: nsys

== openmp compile probe ==
max_threads=2
```

Environment notes:

- `codex-skills/hpc-parallel-c` is a local workspace helper directory and is uncommitted in this PR/report commit; the pasted environment block is the captured `/tmp/minimax-hpc-env.txt` output used as evidence.
- `max_threads=2` comes from the probe script's explicit `OMP_NUM_THREADS=2 "$tmpdir/openmp_probe"` smoke setting, not an observed local CPU or cgroup cap.
- Local CPU affinity and cpuset both expose CPUs `0-15`: `taskset -pc $$` reported `pid 2's current affinity list: 0-15`, and `/sys/fs/cgroup/cpuset.cpus.effective` reported `0-15`.
- No `OMP_*`, `GOMP_*`, `KMP_*`, `OPENBLAS_*`, `MKL_*`, or `NUMEXPR_*` environment variables were set during the controller check.
- `/sys/fs/cgroup/cpu.max` was absent on this host.
- The `uname -a` line above is pasted as literal command output.

## Results

The following sections are appended after each candidate mode is measured.
