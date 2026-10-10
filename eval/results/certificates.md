# RQ7: reusing a compiled program on new data (data certificates)

## Every policy against the fresh run

Cells: correct (ran with the fresh files, or stopped where the fresh run stops) / needless stops (refused, but the fresh compilation runs) / unsound (incl. wrong stops).

| Workload | Batches | fingerprint | file facts | gx/all | gx/read | pandera/all | pandera/read | tfdv/all | tfdv/read | full | deopt |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| daily | 8 | 0 / 8 / 0 | 0 / 8 / 0 | 0 / 8 / 0 | 0 / 8 / 0 | 0 / 8 / 0 | 0 / 8 / 0 | 8 / 0 / 0 | 8 / 0 / 0 | 8 / 0 / 0 | 8 / 0 / 0 |
| students | 24 | 11 / 13 / 0 | 18 / 6 / 0 | 12 / 12 / 0 | 12 / 12 / 0 | 19 / 5 / 0 | 19 / 5 / 0 | 18 / 2 / 4 | 18 / 2 / 4 | 24 / 0 / 0 | 24 / 0 / 0 |
| optimize | 24 | 9 / 15 / 0 | 16 / 8 / 0 | 10 / 14 / 0 | 10 / 14 / 0 | 17 / 7 / 0 | 17 / 7 / 0 | 12 / 3 / 9 | 12 / 3 / 9 | 24 / 0 / 0 | 24 / 0 / 0 |
| ml_split | 48 | 6 / 42 / 0 | 21 / 27 / 0 | 9 / 39 / 0 | 9 / 39 / 0 | 21 / 27 / 0 | 21 / 27 / 0 | 37 / 11 / 0 | 37 / 11 / 0 | 48 / 0 / 0 | 48 / 0 / 0 |
| tableset | 96 | 51 / 45 / 0 | 42 / 29 / 25 | 68 / 28 / 0 | 68 / 28 / 0 | 41 / 30 / 25 | 42 / 29 / 25 | 53 / 10 / 33 | 54 / 9 / 33 | 96 / 0 / 0 | 96 / 0 / 0 |
| parquet | 24 | 21 / 3 / 0 | 24 / 0 / 0 | 22 / 2 / 0 | 24 / 0 / 0 | 22 / 2 / 0 | 24 / 0 / 0 | 18 / 2 / 4 | 20 / 0 / 4 | 24 / 0 / 0 | 24 / 0 / 0 |
| linked_images | 20 | 17 / 3 / 0 | 1 / 3 / 16 | 17 / 3 / 0 | 17 / 3 / 0 | 1 / 3 / 16 | 1 / 3 / 16 | 1 / 3 / 16 | 1 / 3 / 16 | 20 / 0 / 0 | 20 / 0 / 0 |
| **all** | 244 | 115 / 129 / 0 | 122 / 81 / 41 | 138 / 106 / 0 | 140 / 104 / 0 | 121 / 82 / 41 | 124 / 79 / 41 | 147 / 31 / 66 | 150 / 28 / 66 | 244 / 0 / 0 | 244 / 0 / 0 |

Batches that ran with a removed instruction re-enabled: 35

Each batch is run by the program compiled once (with `table=batch`) and, as the ground truth, compiled and run fresh for that batch.

| Workload | Batches | Fingerprint admits | File facts admit | Certificate admits | Admitted and ends like fresh | Unsound |
|---|---:|---:|---:|---:|---:|---:|
| daily | 8 | 0/8 | 0/8 | 8/8 | 8/8 | 0 |
| students | 24 | 0/24 | 7/24 | 24/24 | 24/24 | 0 |
| optimize | 24 | 0/24 | 7/24 | 24/24 | 24/24 | 0 |
| ml_split | 48 | 0/48 | 15/48 | 48/48 | 48/48 | 0 |
| tableset | 96 | 0/96 | 41/96 | 96/96 | 96/96 | 0 |
| parquet | 24 | 0/24 | 10/24 | 24/24 | 24/24 | 0 |
| linked_images | 20 | 0/20 | 16/20 | 20/20 | 20/20 | 0 |
| **all** | 244 | 0/244 | 96/244 | 244/244 | 244/244 | 0 |

## By kind of edit

| Edit | Batches | File facts admit | Certificate admits | Refused, but a fresh compilation runs |
|---|---:|---:|---:|---:|
| another day | 4 | 0/4 | 4/4 | 0/4 |
| another season | 4 | 0/4 | 4/4 | 0/4 |
| resampled | 40 | 40/40 | 40/40 | 0/40 |
| drift small | 40 | 18/40 | 40/40 | 0/40 |
| drift medium | 40 | 12/40 | 40/40 | 0/40 |
| drift large | 40 | 5/40 | 40/40 | 0/40 |
| missing values | 40 | 21/40 | 40/40 | 0/40 |
| out of range | 36 | 0/36 | 36/36 | 0/36 |

*Admitted* counts batches the certificate let through; a deferred or value-level check may still stop such a run, exactly as it stops the fresh compilation. *Unsound* would be a batch the compiled-once program finished on while the fresh compilation failed or wrote different files.
