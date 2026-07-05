# Duel Minimax Parallelization Report

## Baseline

- Date: 2026-07-05
- HPC probe command: `bash codex-skills/hpc-parallel-c/scripts/hpc_env_probe.sh > /tmp/minimax-hpc-env.txt`
- Serial build: `python3 setup.py build_ext --inplace --force`
- Fixed-depth command: `python3 -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 1 --budgets 400 --fixed-depths 6,8 --runs 15 --warmup 3 --out exports/minimax_parallel/serial-fixed-depth.jsonl`
- Live-budget command: `python3 -B tools/benchmark_minimax_parallel_modes.py --modes serial --threads 1 --budgets 180,320,400 --fixed-depths 0 --runs 15 --warmup 3 --out exports/minimax_parallel/serial-budget.jsonl`
- Correctness oracle: fixed-depth serial and parallel modes must match move and score within absolute tolerance `1e-6`.
- Final harness default: `tools/benchmark_minimax_parallel_modes.py` defaults to `serial` only because all candidate implementations were reverted; rejected artifacts remain in this report for auditability.
- Decision checker: `tools/check_minimax_parallel_report.py` enforces move, fixed-depth completion depth, and score correctness before any candidate can receive `decision=keep`.
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

## Local Candidate Measurements

All local candidate artifacts below were measured from temporary experiment
branches and then reverted after the checker decision stayed negative. The
production `StrategyDuel` path is unchanged: no mode passed the local,
compute-node, and final verification gates, so the final benchmark mode list is
`serial` only.

### root_moves

- Artifact: `exports/minimax_parallel/root-moves.jsonl` (`112` rows).
- Reverted diff: `/tmp/root-moves-reverted.diff`.
- Coverage: `7` duel scenarios, fixed depths `6,8`, thread counts `1,2,4,8`.
- Decision: rejected. Best group reached only `2` winning scenarios and every
  group had fixed-depth regressions.

```text
mode=root_moves decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.874 median_speedup=0.697 best_group=1:400:6
mode=root_moves decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.805 median_speedup=0.583 best_group=1:400:8
mode=root_moves decision=revert wins=1 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.120 median_speedup=0.827 best_group=2:400:6
mode=root_moves decision=revert wins=0 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=0.990 median_speedup=0.882 best_group=2:400:8
mode=root_moves decision=revert wins=1 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.090 median_speedup=0.815 best_group=4:400:6
mode=root_moves decision=revert wins=2 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.091 median_speedup=0.781 best_group=4:400:8
mode=root_moves decision=revert wins=0 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=0.926 median_speedup=0.670 best_group=8:400:6
mode=root_moves decision=revert wins=0 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.036 median_speedup=0.725 best_group=8:400:8
```

### pv_root_moves

- Artifact: `exports/minimax_parallel/pv-root-moves.jsonl` (`112` rows).
- Reverted diff: `/tmp/pv-root-moves-reverted.diff`.
- Coverage: `7` duel scenarios, fixed depths `6,8`, thread counts `1,2,4,8`.
- Decision: rejected. Best group reached only `3` winning scenarios and every
  group had fixed-depth regressions.

```text
mode=pv_root_moves decision=revert wins=1 regressions=2 latency_failures=0 missing_baselines=0 best_speedup=1.154 median_speedup=0.950 best_group=1:400:6
mode=pv_root_moves decision=revert wins=0 regressions=4 latency_failures=0 missing_baselines=0 best_speedup=0.983 median_speedup=0.786 best_group=1:400:8
mode=pv_root_moves decision=revert wins=3 regressions=4 latency_failures=0 missing_baselines=0 best_speedup=1.106 median_speedup=0.861 best_group=2:400:6
mode=pv_root_moves decision=revert wins=1 regressions=4 latency_failures=0 missing_baselines=0 best_speedup=1.130 median_speedup=0.877 best_group=2:400:8
mode=pv_root_moves decision=revert wins=0 regressions=3 latency_failures=0 missing_baselines=0 best_speedup=1.013 median_speedup=0.941 best_group=4:400:6
mode=pv_root_moves decision=revert wins=1 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.114 median_speedup=0.713 best_group=4:400:8
mode=pv_root_moves decision=revert wins=0 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.003 median_speedup=0.754 best_group=8:400:6
mode=pv_root_moves decision=revert wins=0 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=1.024 median_speedup=0.800 best_group=8:400:8
```

### root_replies

- Artifact: `exports/minimax_parallel/root-replies.jsonl` (`168` rows).
- Reverted diff: `/tmp/root-replies-reverted.diff`.
- Coverage: `7` duel scenarios, fixed depths `5,6,8`, thread counts
  `1,2,4,8`.
- Decision: rejected. Best group reached only `1` winning scenario and every
  group had fixed-depth regressions.

```text
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.776 median_speedup=0.405 best_group=1:400:5
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.543 median_speedup=0.442 best_group=1:400:6
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.564 median_speedup=0.330 best_group=1:400:8
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.892 median_speedup=0.511 best_group=2:400:5
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.763 median_speedup=0.489 best_group=2:400:6
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.710 median_speedup=0.436 best_group=2:400:8
mode=root_replies decision=revert wins=0 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=0.999 median_speedup=0.751 best_group=4:400:5
mode=root_replies decision=revert wins=1 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=1.126 median_speedup=0.677 best_group=4:400:6
mode=root_replies decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.887 median_speedup=0.608 best_group=4:400:8
mode=root_replies decision=revert wins=0 regressions=5 latency_failures=0 missing_baselines=0 best_speedup=0.938 median_speedup=0.655 best_group=8:400:5
mode=root_replies decision=revert wins=0 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=0.933 median_speedup=0.755 best_group=8:400:6
mode=root_replies decision=revert wins=0 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=1.015 median_speedup=0.534 best_group=8:400:8
```

### ply1_tasks

- Artifact: `exports/minimax_parallel/ply1-tasks.jsonl` (`84` rows).
- Reverted diff: `/tmp/deep-parallel-experiments-reverted.diff`.
- Coverage: `7` duel scenarios, fixed depths `6,8`, thread counts `2,4,8`.
- Decision: rejected. Best group reached only `1` winning scenario and every
  group had fixed-depth regressions.

```text
mode=ply1_tasks decision=revert wins=0 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=0.976 median_speedup=0.651 best_group=2:400:6
mode=ply1_tasks decision=revert wins=0 regressions=7 latency_failures=0 missing_baselines=0 best_speedup=0.787 median_speedup=0.484 best_group=2:400:8
mode=ply1_tasks decision=revert wins=1 regressions=4 latency_failures=0 missing_baselines=0 best_speedup=1.065 median_speedup=0.748 best_group=4:400:6
mode=ply1_tasks decision=revert wins=0 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=1.037 median_speedup=0.648 best_group=4:400:8
mode=ply1_tasks decision=revert wins=0 regressions=4 latency_failures=0 missing_baselines=0 best_speedup=1.031 median_speedup=0.803 best_group=8:400:6
mode=ply1_tasks decision=revert wins=0 regressions=6 latency_failures=0 missing_baselines=0 best_speedup=1.008 median_speedup=0.538 best_group=8:400:8
```

## Rejected Experiments

- `root_moves`: rejected and reverted. Artifact
  `exports/minimax_parallel/root-moves.jsonl`; reverted diff
  `/tmp/root-moves-reverted.diff`. Reason: insufficient winning scenarios and
  repeated fixed-depth regressions.
- `pv_root_moves`: rejected and reverted. Artifact
  `exports/minimax_parallel/pv-root-moves.jsonl`; reverted diff
  `/tmp/pv-root-moves-reverted.diff`. Reason: best group still missed the
  four-scenario keep threshold and had regressions.
- `root_replies`: rejected and reverted. Artifact
  `exports/minimax_parallel/root-replies.jsonl`; reverted diff
  `/tmp/root-replies-reverted.diff`. Reason: broad fixed-depth slowdown across
  depths and thread counts.
- `ply1_tasks`: rejected and reverted. Artifact
  `exports/minimax_parallel/ply1-tasks.jsonl`; reverted diff
  `/tmp/deep-parallel-experiments-reverted.diff`. Reason: insufficient wins and
  fixed-depth regressions on every measured group.
- `leaf_eval`: skipped. No implementation artifact was expected because the
  current counters and evaluator architecture did not expose a safe coarse leaf
  batch boundary.

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
