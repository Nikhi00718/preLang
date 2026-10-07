# RQ7: reusing a compiled program on new data (data certificates)

Each batch is run by the program compiled once (with `table=batch`) and, as the ground truth, compiled and run fresh for that batch.

| Workload | Batches | Fingerprint admits | File facts admit | Certificate admits | Admitted and ends like fresh | Unsound |
|---|---:|---:|---:|---:|---:|---:|
| daily | 8 | 0/8 | 0/8 | 8/8 | 8/8 | 0 |
| students | 24 | 0/24 | 7/24 | 14/24 | 14/14 | 0 |
| optimize | 24 | 0/24 | 7/24 | 15/24 | 15/15 | 0 |
| ml_split | 48 | 0/48 | 15/48 | 42/48 | 42/42 | 0 |
| tableset | 96 | 0/96 | 41/96 | 96/96 | 96/96 | 0 |
| parquet | 24 | 0/24 | 10/24 | 14/24 | 14/14 | 0 |
| linked_images | 20 | 0/20 | 16/20 | 20/20 | 20/20 | 0 |
| **all** | 244 | 0/244 | 96/244 | 209/244 | 209/209 | 0 |

## By kind of edit

| Edit | Batches | File facts admit | Certificate admits | Refused, but a fresh compilation runs |
|---|---:|---:|---:|---:|
| another day | 4 | 0/4 | 4/4 | 0/4 |
| another season | 4 | 0/4 | 4/4 | 0/4 |
| resampled | 40 | 40/40 | 40/40 | 0/40 |
| drift small | 40 | 18/40 | 40/40 | 0/40 |
| drift medium | 40 | 12/40 | 36/40 | 0/40 |
| drift large | 40 | 5/40 | 25/40 | 0/40 |
| missing values | 40 | 21/40 | 38/40 | 0/40 |
| out of range | 36 | 0/36 | 22/36 | 0/36 |

*Admitted* counts batches the certificate let through; a deferred or value-level check may still stop such a run, exactly as it stops the fresh compilation. *Unsound* would be a batch the compiled-once program finished on while the fresh compilation failed or wrote different files.
