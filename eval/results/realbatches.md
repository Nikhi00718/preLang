# RQ8: reusing a compiled pipeline on real monthly batches

Each pipeline is compiled on one month (the first, or the month before) and given the next months with `name=file`; each month is also compiled and run fresh as the ground truth. Every policy is scored against that fresh run (see eval/reuse/policies.py): *correct* = it ran and wrote the fresh files, or it stopped where the fresh run stops; *needless* = it refused a month the fresh compilation runs; *unsound* = it finished with other files than the fresh run, or where the fresh run fails.

| Workload | Anchor | Months | fingerprint | file facts | gx/all | gx/read | pandera/all | pandera/read | tfdv/all | tfdv/read | full | deopt |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| traffic | first | 62 | 2 / 60 / 0 | 2 / 60 / 0 | 2 / 60 / 0 | 2 / 60 / 0 | 2 / 60 / 0 | 2 / 60 / 0 | 2 / 60 / 0 | 52 / 8 / 2 | 61 / 1 / 0 | 62 / 0 / 0 |
| traffic | previous | 60 | 1 / 59 / 0 | 3 / 57 / 0 | 1 / 59 / 0 | 1 / 59 / 0 | 3 / 57 / 0 | 3 / 57 / 0 | 3 / 57 / 0 | 40 / 19 / 1 | 59 / 1 / 0 | 60 / 0 / 0 |
| bikes | first | 23 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 22 / 1 / 0 | 23 / 0 / 0 |
| bikes | previous | 23 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 0 / 23 / 0 | 22 / 1 / 0 | 23 / 0 / 0 |
| taxi | first | 35 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 12 / 0 / 23 | 12 / 0 / 23 | 18 / 17 / 0 | 19 / 16 / 0 |
| taxi | previous | 35 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 7 / 28 / 0 | 34 / 0 / 1 | 34 / 0 / 1 | 33 / 2 / 0 | 34 / 1 / 0 |
| citibike | first | 35 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 20 / 15 / 0 | 35 / 0 / 0 |
| citibike | previous | 35 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 0 / 35 / 0 | 13 / 22 / 0 | 13 / 22 / 0 | 30 / 5 / 0 | 35 / 0 / 0 |
| air | first | 59 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 4 / 55 / 0 | 4 / 55 / 0 | 56 / 3 / 0 | 56 / 3 / 0 |
| air | previous | 59 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 0 / 59 / 0 | 45 / 14 / 0 | 45 / 14 / 0 | 50 / 9 / 0 | 53 / 6 / 0 |
| **all** | | 426 | 17 / 409 / 0 | 19 / 407 / 0 | 17 / 409 / 0 | 17 / 409 / 0 | 19 / 407 / 0 | 19 / 407 / 0 | 113 / 289 / 24 | 200 / 199 / 27 | 371 / 55 / 0 | 400 / 26 / 0 |

Cells: correct / needless stops / unsound (incl. wrong stops).

## Deoptimization

- months that ran with at least one removed instruction re-enabled: 32
  - 20 x line 12: removed because rides.start_station_id has no missing values here
  - 3 x line 4: EXPECT traffic.temp BETWEEN 230 AND 320
  - 3 x line 9: removed because no column of air has missing values here
  - 2 x line 8: removed because the condition is true for every row of traffic
  - 2 x line 8: removed because the condition is true for every row of days
  - 2 x line 10: EXPECT trips.fare_amount BETWEEN 0 AND 1000
- refused by the full certificate but admitted with deoptimization: 32

## Why the deoptimizing program still refused months

- taxi: 24 x its column 'passenger_count' is Int64, not FLOAT. Compile the .prep file again for this input.
- air: 4 x its column 'PRES' is Float64, not INTEGER. Compile the .prep file again for this input.
- air: 2 x its column 'TEMP' is Float64, not INTEGER. Compile the .prep file again for this input.
- air: 2 x its column 'PRES' is Int64, not FLOAT. Compile the .prep file again for this input.
- air: 1 x its column 'TEMP' is Int64, not FLOAT. Compile the .prep file again for this input.

## Time per month (median)

- compiled program with admission and deoptimization: 1.31 s
- fresh compilation and run: 1.40 s
