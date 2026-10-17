# Benchmarks: how we measure

Numbers are only useful if someone else can repeat them. This page is the method; results go in the table at the end
with the date, machine and commit.

## Rules

1. Release build only (`-DCMAKE_BUILD_TYPE=Release`).
2. Close other programs. Plug in a laptop. Note the CPU, GPU, driver and OS.
3. Run each measurement 5 times; report the median and the worst run.
4. Give the commit (`git rev-parse --short HEAD`) and the command used.
5. A change is called "faster" only if the median improves by more than the run-to-run spread.

## What to measure and how

| Measure | Command | Notes |
|---|---|---|
| Cold start (first run after a reboot) | `time ./engine_runtime --self-test` | `--self-test` starts the renderer, draws 10 frames and exits; reboot or drop caches first (`echo 3 | sudo tee /proc/sys/vm/drop_caches` on Linux) |
| Hot start | the same command, run again straight away | caches are warm |
| Tests | `./build/tests/engine_tests` | prints the number of checks and the time |
| Roblox compatibility | `./build/src/kronos_compat` | the score per place |
| Frame time | Studio: Stats panel and F3 overlay; shows CPU and GPU time | GPU time uses timestamp queries |
| Where CPU time goes | Studio Stats > "Where the time goes" | |
| Network | `tests/remote_network_smoke.sh` | correctness, not speed |
| Memory | Stats panel; or `/usr/bin/time -v` | |

## Before and after

For an optimisation, record the command, the median before, the median after and the commit of each, in the table
below, in the same change that makes the optimisation.

## Results

| Date | Commit | Machine | Measure | Median | Worst | Note |
|---|---|---|---|---|---|---|
| | | | | | | (none recorded yet) |
