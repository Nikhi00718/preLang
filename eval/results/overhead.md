# RQ11: the cost of reusing a compiled program

Medians over the months (each month: median of 3 runs); only months every variant finished.

| Workload | Months | Trusted run (s) | Admission, full certificate (s) | Admission + deoptimization (s) | Overhead | Fresh compile (s) | Fresh compile + run (s) |
|---|---:|---:|---:|---:|---:|---:|---:|
| traffic | 59 | 0.97 | 0.96 | 0.97 | 0.0% | 0.09 | 1.10 |
| bikes | 22 | 0.96 | 0.95 | 0.95 | 0.1% | 0.17 | 1.19 |
| taxi | 27 | 1.04 | 1.03 | 1.06 | 1.6% | 0.26 | 1.33 |
