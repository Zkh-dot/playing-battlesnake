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

## Compute Node Performance Gate

- Date: 2026-07-05
- Status: done.
- Host: `scv@192.168.1.6` (`scv-b760mhdvm2`, 24 online CPUs, Intel Core i7-13700K).
- Remote workspace: `~/playing-battlesnake-2-minimax-parallel`.
- Final benchmark mode list: `serial` only.
- Thread counts: `1,2,4,8,16`.
- Native build flags: `CFLAGS="-O3 -march=native -mtune=native -DNDEBUG"`.
- Serial build command: `CFLAGS="-O3 -march=native -mtune=native -DNDEBUG" .venv/bin/python setup.py build_ext --inplace --force`.
- OpenMP build command: `CFLAGS="-O3 -march=native -mtune=native -DNDEBUG" BATTLESNAKE_ENABLE_MINIMAX_OPENMP=1 .venv/bin/python setup.py build_ext --inplace --force`.
- Correctness matrix: `OMP_NUM_THREADS=1`, `2`, and `4` with `.venv/bin/python -B -m unittest discover -v`; each run passed 29 tests.
- Fixed-depth performance command: `.venv/bin/python -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 1,2,4,8,16 --budgets 400 --fixed-depths 6,8 --runs 15 --warmup 3 --out exports/minimax_parallel/compute-node-fixed-depth.jsonl`.
- Live-budget performance command: `.venv/bin/python -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 1,2,4,8,16 --budgets 180,320,400 --fixed-depths 0 --runs 15 --warmup 3 --out exports/minimax_parallel/compute-node-budget.jsonl`.
- Decision checker: `.venv/bin/python -B tools/check_minimax_parallel_report.py --results ...` printed no lines for both compute-node artifacts, which is expected because the final mode list is serial-only and there are no candidate rows.

Compute-node artifacts copied back locally:

- `exports/minimax_parallel/compute-node-fixed-depth.jsonl` (`70` rows).
- `exports/minimax_parallel/compute-node-budget.jsonl` (`105` rows).
- `exports/minimax_parallel/compute-node-env.txt` (`59` lines).

Notes:

- The workspace was synced with `rsync -a` excluding `.git`, `.venv`, `build`, `*.so`, and `exports/minimax_parallel`, so the compute node did not reuse the local native extension or local minimax export artifacts.
- The first system-Python correctness attempt failed before search validation because `pydantic` was missing on the compute node. A remote `.venv` was created, project requirements were installed, both required native builds were repeated inside that `.venv`, and the correctness matrix then passed.
- No locally kept parallel candidate mode remained to validate on the compute node. The reverted modes (`root_moves`, `pv_root_moves`, `root_replies`, `ply1_tasks`) and skipped `leaf_eval` were intentionally excluded from `--modes`.

## Final ya.sergeiscv.ru Build Verification

- Date: 2026-07-05.
- Status: done.
- Host: `ya.sergeiscv.ru` (`compute-vm-4-8-100-ssd-1759320606973`, 4 online CPUs, Intel Xeon Processor Icelake).
- Remote workspace: `~/playing-battlesnake-2-minimax-final`.
- Sync rule: `rsync -a` from the final branch excluding `.git`, `.venv`, `build`, `*.so`, and `exports/minimax_parallel`; no local or compute-node native extension was reused.
- Environment probe: `bash codex-skills/hpc-parallel-c/scripts/hpc_env_probe.sh > /tmp/minimax-ya-hpc-env.txt && cat /tmp/minimax-ya-hpc-env.txt`.
- Native OpenMP build command: `CFLAGS="-O3 -march=native -mtune=native -DNDEBUG" BATTLESNAKE_ENABLE_MINIMAX_OPENMP=1 python setup.py build_ext --inplace --force` inside the remote `.venv`.
- Final benchmark mode list: `serial`.
- Required thread checks: `4` and `8`.
- Unit tests: `OMP_NUM_THREADS=4 python -B -m unittest discover -v` passed 29 tests; `OMP_NUM_THREADS=8 python -B -m unittest discover -v` passed 29 tests.
- Smoke command: `python -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 4,8 --budgets 400 --fixed-depths 6 --scenarios duel_open_7x7,duel_late_game_long_bodies --runs 5 --warmup 1 --out exports/minimax_parallel/ya-final-smoke.jsonl`.
- Smoke artifact: `exports/minimax_parallel/ya-final-smoke.jsonl` (`4` rows).
- Environment artifact: `exports/minimax_parallel/ya-env.txt` (`49` lines).
- Result: final host-native OpenMP build verification passed for the `serial`-only final mode list on both required thread settings.

Notes:

- The remote environment had GCC 12.2.0 and a working OpenMP compile probe (`max_threads=2` under the probe script's explicit `OMP_NUM_THREADS=2` setting).
- `clang`, `mpicc`, `mpirun`, `perf`, and `nsys` were missing on `ya.sergeiscv.ru`; they were not required for this final OpenMP build, unit-test, or smoke-benchmark verification.
