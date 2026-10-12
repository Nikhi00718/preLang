# RQ10: what a private compilation tells the analyst

A 1,000,000-row Parquet file (eval/data/events_1000000.parquet) with a mistake injected into m rows; the program has three contracts, one of which the mistake breaks. A normal compilation reports that contract as violated for every m > 0. Cells: the share of private compilations (5 seeds; 1 without noise) that warn that the broken contract is likely violated; in brackets, warnings about a contract the data does not break.

| Mistake | m | epsilon 0.1 | epsilon 1 | epsilon 10 | epsilon inf |
|---|---:|---:|---:|---:|---:|
| amount above 999 | 0 | 0% | 0% | 0% | 0% |
| amount above 999 | 1 | 0% | 0% | 0% | 100% |
| amount above 999 | 10 | 0% | 0% | 0% | 100% |
| amount above 999 | 100 | 0% | 0% | 100% | 100% |
| amount above 999 | 1000 | 0% | 100% | 100% | 100% |
| amount above 999 | 10000 | 100% | 100% | 100% | 100% |
| s missing | 0 | 0% | 0% | 0% | 0% |
| s missing | 1 | 0% | 0% | 0% | 100% |
| s missing | 10 | 0% | 0% | 0% | 100% |
| s missing | 100 | 0% | 0% | 100% | 100% |
| s missing | 1000 | 0% | 100% | 100% | 100% |
| s missing | 10000 | 100% | 100% | 100% | 100% |
| a below 0 | 0 | 0% | 0% | 0% | 0% |
| a below 0 | 1 | 0% | 0% | 0% | 100% |
| a below 0 | 10 | 0% | 0% | 0% | 100% |
| a below 0 | 100 | 0% | 0% | 100% | 100% |
| a below 0 | 1000 | 0% | 100% | 100% | 100% |
| a below 0 | 10000 | 100% | 100% | 100% | 100% |
