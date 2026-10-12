# RQ10: private compilation

The analyst compiles from facts the data owner released (tools/release_facts.py), in a folder without the data; the data owner runs the generated program on every batch. Each run is scored against a fresh compilation of the batch, run (eval/reuse/policies.py). *Removals*: the removed instructions of the private compilation against those of the normal compilation of the compiled file (precision, recall). *Deoptimized on the compiled file*: runs on the very file the facts describe in which a removal had to run again because a released fact was false.

| Workload | Facts | Compilations | Runs | Correct run | Correct stop | Needless stop | Wrong stop | Unsound | Removals precision | Removals recall | Deoptimized on the compiled file | Likely violated |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| rq8:bikes | eps=0.1 | 3/3 | 72 | 72 | 0 | 0 | 0 | 0 | 100% | 14% | 0/3 | 0 |
| rq8:bikes | eps=1 | 3/3 | 72 | 72 | 0 | 0 | 0 | 0 | 100% | 14% | 0/3 | 0 |
| rq8:bikes | eps=10 | 3/3 | 72 | 72 | 0 | 0 | 0 | 0 | 100% | 14% | 0/3 | 0 |
| rq8:bikes | eps=100 | 3/3 | 72 | 72 | 0 | 0 | 0 | 0 | 100% | 100% | 0/3 | 0 |
| rq8:bikes | exact | 1/1 | 24 | 24 | 0 | 0 | 0 | 0 | 100% | 100% | 0/1 | 0 |
| rq8:bikes | none | 1/1 | 24 | 24 | 0 | 0 | 0 | 0 | - | 0% | 0/1 | 0 |
| rq8:bikes | random | 10/10 | 240 | 240 | 0 | 0 | 0 | 0 | 100% | 30% | 0/10 | 0 |
| rq8:taxi | eps=0.1 | 3/3 | 108 | 39 | 21 | 48 | 0 | 0 | 33% | 100% | 3/3 | 0 |
| rq8:taxi | eps=1 | 3/3 | 108 | 39 | 21 | 48 | 0 | 0 | 100% | 100% | 0/3 | 0 |
| rq8:taxi | eps=10 | 3/3 | 108 | 39 | 21 | 48 | 0 | 0 | 100% | 100% | 0/3 | 0 |
| rq8:taxi | eps=100 | 3/3 | 108 | 39 | 21 | 48 | 0 | 0 | 100% | 100% | 0/3 | 0 |
| rq8:taxi | exact | 1/1 | 36 | 13 | 7 | 16 | 0 | 0 | 100% | 100% | 0/1 | 0 |
| rq8:taxi | none | 1/1 | 36 | 13 | 7 | 16 | 0 | 0 | - | 0% | 0/1 | 0 |
| rq8:taxi | random | 10/10 | 360 | 130 | 70 | 160 | 0 | 0 | 58% | 55% | 8/10 | 0 |
| rq8:traffic | eps=0.1 | 3/3 | 189 | 183 | 6 | 0 | 0 | 0 | 100% | 29% | 0/3 | 0 |
| rq8:traffic | eps=1 | 3/3 | 189 | 183 | 6 | 0 | 0 | 0 | 100% | 71% | 0/3 | 0 |
| rq8:traffic | eps=10 | 3/3 | 189 | 183 | 6 | 0 | 0 | 0 | 100% | 100% | 0/3 | 0 |
| rq8:traffic | eps=100 | 3/3 | 189 | 183 | 6 | 0 | 0 | 0 | 100% | 100% | 0/3 | 0 |
| rq8:traffic | exact | 1/1 | 63 | 61 | 2 | 0 | 0 | 0 | 100% | 100% | 0/1 | 0 |
| rq8:traffic | none | 1/1 | 63 | 61 | 2 | 0 | 0 | 0 | - | 0% | 0/1 | 0 |
| rq8:traffic | random | 10/10 | 630 | 610 | 20 | 0 | 0 | 0 | 100% | 26% | 0/10 | 0 |
| titanic | eps=1 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 9% | 100% | 85/184 | 0 |
| titanic | eps=10 | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 15% | 100% | 74/184 | 0 |
| titanic | exact | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | 100% | 100% | 0/184 | 0 |
| titanic | none | 184/184 | 2208 | 2196 | 12 | 0 | 0 | 0 | - | 0% | 0/184 | 0 |
| titanic | random | 368/368 | 4416 | 4392 | 24 | 0 | 0 | 0 | 21% | 44% | 43/368 | 0 |

All runs: 16200; correct run: 15528, correct stop: 288, needless stop: 384, wrong stop: 0, unsound: 0.
