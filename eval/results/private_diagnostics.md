# RQ8: what a private compilation tells the analyst

A 1,000,000-row Parquet file (eval/data/events_1000000.parquet) with a mistake injected into m rows; the program has three contracts, one of which the mistake breaks. A normal compilation reports that contract as violated for every m > 0. Cells: the share of compilations that report the broken contract (a private compilation warns that it is likely violated; one from synthetic data reports it violated); in brackets, reports about a contract the data does not break. *program*: the release at the program's numbers (20 seeds; 1 without noise); *generic* and *synthetic*: the baselines of eval/reuse/private_baselines.py with the same budget (10 seeds).

| Mistake | m | program 0.1 | program 0.3 | program 1 | program 3 | program 10 | program inf | generic 0.1 | generic 0.3 | generic 1 | generic 3 | generic 10 | synthetic 0.1 | synthetic 0.3 | synthetic 1 | synthetic 3 | synthetic 10 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| amount above 999 | 0 | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| amount above 999 | 1 | 0% | 0% | 0% | 0% | 0% | 100% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| amount above 999 | 10 | 0% | 0% | 0% | 80% | 100% | 100% | 0% | 0% | 0% | 50% | 100% | 0% | 0% | 0% | 50% | 100% |
| amount above 999 | 100 | 0% | 80% | 100% | 100% | 100% | 100% | 0% | 50% | 100% | 100% | 100% | 0% | 50% | 100% | 100% | 100% |
| amount above 999 | 1000 | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% |
| amount above 999 | 10000 | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% |
| s missing | 0 | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| s missing | 1 | 0% | 0% | 0% | 0% | 0% | 100% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| s missing | 10 | 0% | 0% | 0% | 80% | 100% | 100% | 0% | 0% | 0% | 60% | 100% | 0% | 0% | 0% | 60% | 100% |
| s missing | 100 | 0% | 80% | 100% | 100% | 100% | 100% | 0% | 60% | 100% | 100% | 100% | 0% | 60% | 100% | 100% | 100% |
| s missing | 1000 | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% |
| s missing | 10000 | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% |
| a below 0 | 0 | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| a below 0 | 1 | 0% | 0% | 0% | 0% | 0% | 100% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| a below 0 | 10 | 0% | 0% | 0% | 90% | 100% | 100% | 0% | 0% | 0% | 40% | 100% | 0% | 0% | 0% | 40% | 100% |
| a below 0 | 100 | 0% | 100% | 100% | 100% | 100% | 100% | 0% | 60% | 100% | 100% | 100% | 0% | 60% | 100% | 100% | 100% |
| a below 0 | 1000 | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% |
| a below 0 | 10000 | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% |
