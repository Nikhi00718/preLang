# RQ8: data certificates on real monthly batches

Each pipeline is compiled on one month (the first month, or the month before) and given the next months with `name=file`; each month is also compiled and run fresh as the ground truth.

| Workload | Anchor | Batches | Fingerprint | File facts | Certificate | Ends like fresh | Unsound | Refused, fresh runs |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| traffic | first | 62 | 0 | 0 | 59 | 59 | 0 | 1 |
| traffic | previous | 60 | 0 | 2 | 58 | 58 | 0 | 1 |
| bikes | first | 23 | 0 | 0 | 22 | 22 | 0 | 1 |
| bikes | previous | 23 | 0 | 0 | 22 | 22 | 0 | 1 |
| taxi | first | 35 | 0 | 0 | 11 | 11 | 0 | 17 |
| taxi | previous | 35 | 0 | 0 | 33 | 33 | 0 | 2 |

## Why batches were refused

- traffic: 3 x line 4: EXPECT traffic.temp BETWEEN 230 AND 320
- traffic: 2 x line 8: removed because the condition is true for every row of traffic
- bikes: 2 x line 4: EXPECT days.hum BETWEEN 0 AND 1; line 8: removed because the condition is true for every row of days
- taxi: 2 x line 10: EXPECT trips.fare_amount BETWEEN 0 AND 1000
- taxi: 2 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-02.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-03.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-04.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-05.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-06.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-07.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-08.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-09.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-10.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-11.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2023-12.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-01.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-02.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-03.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-04.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-05.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-06.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-07.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-08.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-09.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-10.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-11.parquet is not the input t
- taxi: 1 x ContractError: C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang\eval\data\real\green\2024-12.parquet is not the input t
