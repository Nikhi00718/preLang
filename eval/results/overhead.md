# RQ11: the cost of reusing a compiled program

Medians over the months (each month: median of 3 runs); only months every variant finished.

| Workload | Months | Trusted run (s) | Admission, full certificate (s) | Admission + deoptimization (s) | Overhead | Fresh compile (s) | Fresh compile + run (s) |
|---|---:|---:|---:|---:|---:|---:|---:|
| traffic | 59 | 0.90 | 0.89 | 0.89 | -1.0% | 0.03 | 0.95 |
| bikes | 22 | 0.84 | 0.83 | 0.84 | -1.0% | 0.03 | 0.93 |
| taxi | 11 | 0.93 | 0.93 | 0.94 | -0.4% | 0.04 | 1.02 |
