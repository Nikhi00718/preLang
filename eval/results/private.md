# RQ8: private compilation

The analyst compiles from facts the data owner released (tools/release_facts.py), in a folder without the data; the data owner runs the generated program on every batch. Each run is scored against a fresh compilation of the batch, run (eval/reuse/policies.py). *Removals*: the removed instructions of the private compilation against those of the normal compilation of the compiled file (precision, recall). *Deoptimized on the compiled file*: runs on the very file the facts describe in which a removal had to run again because a released fact was false.

| Workload | Facts | Compilations | Runs | Correct run | Correct stop | Needless stop | Wrong stop | Unsound | Removals precision | Removals recall | Deoptimized on the compiled file | Likely violated |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| rq8:bikes | aim eps=1 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 54% | 0/5 | 7 |
| rq8:bikes | aim eps=10 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 69% | 0/5 | 9 |
| rq8:bikes | aim-marginals eps=1 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 14% | 0/5 | 0 |
| rq8:bikes | aim-marginals eps=10 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 80% | 0/5 | 0 |
| rq8:bikes | aim-synthetic eps=1 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:bikes | aim-synthetic eps=10 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:bikes | eps=0.1 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 14% | 0/5 | 0 |
| rq8:bikes | eps=1 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 14% | 0/5 | 0 |
| rq8:bikes | eps=10 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:bikes | eps=100 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:bikes | exact | 1/1 | 24 | 24 | 0 | 0 | 0 | 0 | 100% | 100% | 0/1 | 0 |
| rq8:bikes | generic eps=1 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 14% | 0/5 | 0 |
| rq8:bikes | generic eps=10 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 86% | 0/5 | 0 |
| rq8:bikes | hdmm eps=1 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 14% | 0/5 | 0 |
| rq8:bikes | hdmm eps=10 | 5/5 | 120 | 120 | 0 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:bikes | none | 1/1 | 24 | 24 | 0 | 0 | 0 | 0 | - | 0% | 0/1 | 0 |
| rq8:bikes | optimistic | 1/1 | 24 | 24 | 0 | 0 | 0 | 0 | 100% | 14% | 0/1 | 0 |
| rq8:bikes | random | 10/10 | 240 | 240 | 0 | 0 | 0 | 0 | 100% | 30% | 0/10 | 0 |
| rq8:bikes | synthetic eps=1 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:bikes | synthetic eps=10 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:taxi | aim eps=1 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 56% | 0/5 | 9 |
| rq8:taxi | aim eps=10 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 20% | 0/5 | 16 |
| rq8:taxi | aim-marginals eps=1 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | aim-marginals eps=10 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | aim-synthetic eps=1 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:taxi | aim-synthetic eps=10 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:taxi | eps=0.1 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | eps=1 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | eps=10 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | eps=100 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | exact | 1/1 | 36 | 13 | 7 | 16 | 0 | 0 | 100% | 100% | 0/1 | 0 |
| rq8:taxi | generic eps=1 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 60% | 0/5 | 0 |
| rq8:taxi | generic eps=10 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 60% | 0/5 | 0 |
| rq8:taxi | hdmm eps=1 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | hdmm eps=10 | 5/5 | 180 | 65 | 35 | 80 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:taxi | none | 1/1 | 36 | 13 | 7 | 16 | 0 | 0 | - | 0% | 0/1 | 0 |
| rq8:taxi | optimistic | 1/1 | 36 | 13 | 7 | 16 | 0 | 0 | 50% | 40% | 1/1 | 0 |
| rq8:taxi | random | 10/10 | 360 | 130 | 70 | 160 | 0 | 0 | 64% | 56% | 8/10 | 0 |
| rq8:taxi | synthetic eps=1 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:taxi | synthetic eps=10 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:traffic | aim eps=1 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 26% | 0/5 | 18 |
| rq8:traffic | aim eps=10 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 49% | 0/5 | 10 |
| rq8:traffic | aim-marginals eps=1 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | aim-marginals eps=10 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | aim-synthetic eps=1 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:traffic | aim-synthetic eps=10 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:traffic | eps=0.1 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | eps=1 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | eps=10 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | eps=100 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | exact | 1/1 | 63 | 61 | 2 | 0 | 0 | 0 | 100% | 100% | 0/1 | 0 |
| rq8:traffic | generic eps=1 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 83% | 0/5 | 0 |
| rq8:traffic | generic eps=10 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 71% | 0/5 | 0 |
| rq8:traffic | hdmm eps=1 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | hdmm eps=10 | 5/5 | 315 | 305 | 10 | 0 | 0 | 0 | 100% | 100% | 0/5 | 0 |
| rq8:traffic | none | 1/1 | 63 | 61 | 2 | 0 | 0 | 0 | - | 0% | 0/1 | 0 |
| rq8:traffic | optimistic | 1/1 | 63 | 61 | 2 | 0 | 0 | 0 | 100% | 29% | 0/1 | 0 |
| rq8:traffic | random | 10/10 | 630 | 610 | 20 | 0 | 0 | 0 | 100% | 26% | 0/10 | 0 |
| rq8:traffic | synthetic eps=1 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| rq8:traffic | synthetic eps=10 | 0/5 | 0 | 0 | 0 | 0 | 0 | 0 | - | - | 0/0 | 0 |
| titanic | aim eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 18% | 25% | 18/184 | 0 |
| titanic | aim eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 39% | 44% | 11/184 | 0 |
| titanic | aim-marginals eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 16% | 100% | 74/184 | 0 |
| titanic | aim-marginals eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 18% | 100% | 69/184 | 0 |
| titanic | aim-synthetic eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 9% | 12% | 20/184 | 0 |
| titanic | aim-synthetic eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 47% | 50% | 9/184 | 0 |
| titanic | corpus eps=1 | 368/368 | 4416 | 4392 | 24 | 0 | 0 | 0 | 17% | 100% | 148/368 | 0 |
| titanic | corpus eps=10 | 368/368 | 4416 | 4392 | 24 | 0 | 0 | 0 | 17% | 100% | 148/368 | 0 |
| titanic | eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 17% | 100% | 74/184 | 0 |
| titanic | eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 31% | 100% | 31/184 | 0 |
| titanic | exact | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 100% | 100% | 0/184 | 0 |
| titanic | generic eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 17% | 100% | 74/184 | 0 |
| titanic | generic eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 20% | 100% | 59/184 | 0 |
| titanic | hdmm eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 17% | 100% | 74/184 | 0 |
| titanic | hdmm eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 20% | 100% | 58/184 | 0 |
| titanic | none | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | - | 0% | 0/184 | 0 |
| titanic | optimistic | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 9% | 100% | 85/184 | 0 |
| titanic | random | 368/368 | 4416 | 4392 | 24 | 0 | 0 | 0 | 21% | 44% | 43/368 | 0 |
| titanic | synthetic eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 17% | 100% | 74/184 | 0 |
| titanic | synthetic eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 20% | 100% | 59/184 | 0 |
| titanic | unit eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 17% | 100% | 74/184 | 0 |
| titanic | unit eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 17% | 100% | 73/184 | 0 |

All runs: 64179; correct run: 62054, correct stop: 957, needless stop: 1168, wrong stop: 0, unsound: 0.

## Private compilations that failed

- rq8 traffic synthetic eps=1 seed 0: error[contract]: contract violated: traffic.temp has values from 100.0864365 to 999.8120292; row 1 of traffic.csv has 867.0741696215723
- rq8 traffic synthetic eps=1 seed 1: error[contract]: contract violated: traffic.temp has values from 101.9323384 to 999.5961706; row 1 of traffic.csv has 515.3192979171922
- rq8 traffic synthetic eps=1 seed 2: error[contract]: contract violated: traffic.temp has values from 100.28825 to 998.5835324; row 1 of traffic.csv has 103.5204475204201
- rq8 traffic synthetic eps=1 seed 3: error[contract]: contract violated: traffic.temp has values from 100.5289522 to 999.170763; row 1 of traffic.csv has 159.06633978171413; error[contract]: contract violated: traffic.clouds_all has valu
- rq8 traffic synthetic eps=1 seed 4: error[contract]: contract violated: traffic.temp has values from 101.4276313 to 999.4431185; row 1 of traffic.csv has 572.2611896057642
- rq8 traffic synthetic eps=10 seed 0: error[contract]: contract violated: traffic.temp has values from 100.0864365 to 999.8120292; row 1 of traffic.csv has 148.09846283499678; error[contract]: contract violated: traffic.clouds_all has val
- rq8 traffic synthetic eps=10 seed 1: error[contract]: contract violated: traffic.temp has values from 101.6005273 to 999.5961706; row 1 of traffic.csv has 421.9283785928914; error[contract]: contract violated: traffic.clouds_all has valu
- rq8 traffic synthetic eps=10 seed 2: error[contract]: contract violated: traffic.temp has values from 100.7011947 to 998.5835324; row 1 of traffic.csv has 606.8524835026894; error[contract]: contract violated: traffic.clouds_all has valu
- rq8 traffic synthetic eps=10 seed 3: error[contract]: contract violated: traffic.temp has values from 100.5289522 to 999.170763; row 1 of traffic.csv has 436.7513212172264; error[contract]: contract violated: traffic.clouds_all has value
- rq8 traffic synthetic eps=10 seed 4: error[contract]: contract violated: traffic.temp has values from 101.4276313 to 999.4431185; row 1 of traffic.csv has 320.35170206324983; error[contract]: contract violated: traffic.clouds_all has val
- rq8 bikes synthetic eps=1 seed 0: error[contract]: contract violated: days.cnt has values from -8634738 to 8029637; row 2 of days.csv has -1; error[contract]: contract violated: days.hum has values from -9901172.341 to 8391714.081; ro
- rq8 bikes synthetic eps=1 seed 1: error[contract]: contract violated: days.cnt has values from -3444448 to 9388674; row 1 of days.csv has -77; error[contract]: contract violated: days.hum has values from -3383247.545 to 8422426.617; r
- rq8 bikes synthetic eps=1 seed 2: error[contract]: contract violated: days.cnt has values from -9828303 to 9956093; row 1 of days.csv has -1; error[contract]: contract violated: days.hum has values from -9813118.524 to 7874809.903; ro
- rq8 bikes synthetic eps=1 seed 3: error[contract]: contract violated: days.cnt has values from -8037100 to 9708905; row 1 of days.csv has -1; error[contract]: contract violated: days.hum has values from -8859902.206 to 9564665.217; ro
- rq8 bikes synthetic eps=1 seed 4: error[contract]: contract violated: days.cnt has values from -7892201 to 6940211; row 1 of days.csv has -4; error[contract]: contract violated: days.hum has values from -9834109.383 to 9985186.033; ro
- rq8 bikes synthetic eps=10 seed 0: error[contract]: contract violated: days.weathersit has values from 1 to 9; row 2 of days.csv has 8
- rq8 bikes synthetic eps=10 seed 1: error[contract]: contract violated: days.weathersit has values from 1 to 9; row 1 of days.csv has 6
- rq8 bikes synthetic eps=10 seed 2: error[contract]: contract violated: days.weathersit has values from 1 to 9; row 3 of days.csv has 7
- rq8 bikes synthetic eps=10 seed 3: error[contract]: contract violated: days.weathersit has values from 1 to 9; row 1 of days.csv has 7
- rq8 bikes synthetic eps=10 seed 4: error[contract]: contract violated: days.weathersit has values from 1 to 9; row 2 of days.csv has 5
