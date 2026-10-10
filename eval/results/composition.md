# RQ10: composing certificates across pipeline stages

Stage 1 cleans a month (eval/realbatches.py), stage 2 builds features from its output. Both are compiled once on the first month; tools/contracts.py compares what stage 2 relies on with stage 1's guarantee. *Strengthened*: stage 1 with the EXPECT statements the tool suggested.

| Workload | Stage 1 | Facts stage 2 relies on | Guaranteed | Months through both | Stage 2 refused | Stage 2 deoptimized | ... by a guaranteed fact | Stage 1 stopped | Stage 2 like fresh |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| taxi | as written | 13 | 11 | 15 | 0 | 0 | 0 | 7 | 15 |
| taxi | strengthened | 13 | 13 | 15 | 0 | 0 | 0 | 7 | 15 |
| traffic | as written | 14 | 12 | 60 | 0 | 0 | 0 | 2 | 60 |
| traffic | strengthened | 14 | 14 | 60 | 0 | 0 | 0 | 2 | 60 |
| bikes | as written | 11 | 10 | 23 | 0 | 0 | 0 | 0 | 23 |
| bikes | strengthened | 11 | 11 | 23 | 0 | 0 | 0 | 0 | 23 |

## What the composition reported

### taxi (as written)

```
trips_clean.parquet -> trips: 11 of 13 facts guaranteed by preplang_contract.json
  guaranteed   passenger_count present  (admission: admission)
  guaranteed   trip_distance present  (admission: admission)
  guaranteed   fare_amount present  (admission: admission)
  guaranteed   tip_amount present  (admission: admission)
  guaranteed   payment_type present  (admission: admission)
  guaranteed   fare_amount min 0  (removal: line 3: EXPECT trips.fare_amount BETWEEN 0 AND 1000)
  guaranteed   fare_amount max 1000  (removal: line 3: EXPECT trips.fare_amount BETWEEN 0 AND 1000)
  guaranteed   passenger_count not_null  (removal: line 4: EXPECT trips.passenger_count NOT_NULL)
  guaranteed   trip_distance min 0  (removal: line 5: EXPECT trips.trip_distance BETWEEN 0 AND 100)
  guaranteed   trip_distance max 100  (removal: line 5: EXPECT trips.trip_distance BETWEEN 0 AND 100)
  guaranteed   payment_type not_null  (removal: line 7: removed because trips.payment_type has no missing values here)
  NOT implied  tip_amount min 0  (removal: line 6: EXPECT trips.tip_amount BETWEEN 0 AND 500)
  NOT implied  tip_amount max 500  (removal: line 6: EXPECT trips.tip_amount BETWEEN 0 AND 500)
  stage 1 would guarantee them with, before its SAVE:
    EXPECT trips.tip_amount BETWEEN 0 AND 500;
2 fact(s) not guaranteed: stage 2 may refuse or deoptimize
```

### taxi (strengthened)

```
trips_clean.parquet -> trips: 13 of 13 facts guaranteed by preplang_contract.json
  guaranteed   passenger_count present  (admission: admission)
  guaranteed   trip_distance present  (admission: admission)
  guaranteed   fare_amount present  (admission: admission)
  guaranteed   tip_amount present  (admission: admission)
  guaranteed   payment_type present  (admission: admission)
  guaranteed   fare_amount min 0  (removal: line 3: EXPECT trips.fare_amount BETWEEN 0 AND 1000)
  guaranteed   fare_amount max 1000  (removal: line 3: EXPECT trips.fare_amount BETWEEN 0 AND 1000)
  guaranteed   passenger_count not_null  (removal: line 4: EXPECT trips.passenger_count NOT_NULL)
  guaranteed   trip_distance min 0  (removal: line 5: EXPECT trips.trip_distance BETWEEN 0 AND 100)
  guaranteed   trip_distance max 100  (removal: line 5: EXPECT trips.trip_distance BETWEEN 0 AND 100)
  guaranteed   tip_amount min 0  (removal: line 6: EXPECT trips.tip_amount BETWEEN 0 AND 500)
  guaranteed   tip_amount max 500  (removal: line 6: EXPECT trips.tip_amount BETWEEN 0 AND 500)
  guaranteed   payment_type not_null  (removal: line 7: removed because trips.payment_type has no missing values here)
composes: stage 2 never refuses or deoptimizes a stage-1 output for its values
```

### traffic (as written)

```
traffic_clean.csv -> traffic: 12 of 14 facts guaranteed by preplang_contract.json
  guaranteed   temp present  (admission: admission)
  guaranteed   rain_1h present  (admission: admission)
  guaranteed   clouds_all present  (admission: admission)
  guaranteed   weather_main present  (admission: admission)
  guaranteed   traffic_volume present  (admission: admission)
  guaranteed   traffic_volume min 0  (removal: line 3: EXPECT traffic.traffic_volume BETWEEN 0 AND 1)
  guaranteed   traffic_volume max 1  (removal: line 3: EXPECT traffic.traffic_volume BETWEEN 0 AND 1)
  guaranteed   weather_main not_null  (removal: line 4: EXPECT traffic.weather_main NOT_NULL)
  guaranteed   rain_1h max 100  (removal: line 5: EXPECT traffic.rain_1h BETWEEN 0 AND 100)
  guaranteed   temp min 230  (removal: line 6: EXPECT traffic.temp BETWEEN 230 AND 320)
  guaranteed   temp max 320  (removal: line 6: EXPECT traffic.temp BETWEEN 230 AND 320)
  guaranteed   clouds_all max 100  (removal: line 7: removed because the condition is true for every row of traffic)
  NOT implied  rain_1h min 0  (removal: line 5: EXPECT traffic.rain_1h BETWEEN 0 AND 100)
  NOT implied  clouds_all not_null  (removal: line 7: removed because the condition is true for every row of traffic)
  stage 1 would guarantee them with, before its SAVE:
    EXPECT traffic.rain_1h BETWEEN 0 AND 100;
    EXPECT traffic.clouds_all NOT_NULL;
2 fact(s) not guaranteed: stage 2 may refuse or deoptimize
```

### traffic (strengthened)

```
traffic_clean.csv -> traffic: 14 of 14 facts guaranteed by preplang_contract.json
  guaranteed   temp present  (admission: admission)
  guaranteed   rain_1h present  (admission: admission)
  guaranteed   clouds_all present  (admission: admission)
  guaranteed   weather_main present  (admission: admission)
  guaranteed   traffic_volume present  (admission: admission)
  guaranteed   traffic_volume min 0  (removal: line 3: EXPECT traffic.traffic_volume BETWEEN 0 AND 1)
  guaranteed   traffic_volume max 1  (removal: line 3: EXPECT traffic.traffic_volume BETWEEN 0 AND 1)
  guaranteed   weather_main not_null  (removal: line 4: EXPECT traffic.weather_main NOT_NULL)
  guaranteed   rain_1h min 0  (removal: line 5: EXPECT traffic.rain_1h BETWEEN 0 AND 100)
  guaranteed   rain_1h max 100  (removal: line 5: EXPECT traffic.rain_1h BETWEEN 0 AND 100)
  guaranteed   temp min 230  (removal: line 6: EXPECT traffic.temp BETWEEN 230 AND 320)
  guaranteed   temp max 320  (removal: line 6: EXPECT traffic.temp BETWEEN 230 AND 320)
  guaranteed   clouds_all not_null  (removal: line 7: removed because the condition is true for every row of traffic)
  guaranteed   clouds_all max 100  (removal: line 7: removed because the condition is true for every row of traffic)
composes: stage 2 never refuses or deoptimizes a stage-1 output for its values
```

### bikes (as written)

```
days_clean.csv -> days: 10 of 11 facts guaranteed by preplang_contract.json
  guaranteed   weathersit present  (admission: admission)
  guaranteed   temp present  (admission: admission)
  guaranteed   hum present  (admission: admission)
  guaranteed   windspeed present  (admission: admission)
  guaranteed   hum min 0  (removal: line 3: EXPECT days.hum BETWEEN 0 AND 1)
  guaranteed   hum max 1  (removal: line 3: EXPECT days.hum BETWEEN 0 AND 1)
  guaranteed   windspeed not_null  (removal: line 4: EXPECT days.windspeed NOT_NULL)
  guaranteed   weathersit min 1  (removal: line 5: EXPECT days.weathersit BETWEEN 1 AND 4)
  guaranteed   weathersit max 4  (removal: line 5: EXPECT days.weathersit BETWEEN 1 AND 4)
  guaranteed   temp min 0  (removal: line 6: removed because the condition is true for every row of days)
  NOT implied  temp not_null  (removal: line 6: removed because the condition is true for every row of days)
  stage 1 would guarantee them with, before its SAVE:
    EXPECT days.temp NOT_NULL;
1 fact(s) not guaranteed: stage 2 may refuse or deoptimize
```

### bikes (strengthened)

```
days_clean.csv -> days: 11 of 11 facts guaranteed by preplang_contract.json
  guaranteed   weathersit present  (admission: admission)
  guaranteed   temp present  (admission: admission)
  guaranteed   hum present  (admission: admission)
  guaranteed   windspeed present  (admission: admission)
  guaranteed   hum min 0  (removal: line 3: EXPECT days.hum BETWEEN 0 AND 1)
  guaranteed   hum max 1  (removal: line 3: EXPECT days.hum BETWEEN 0 AND 1)
  guaranteed   windspeed not_null  (removal: line 4: EXPECT days.windspeed NOT_NULL)
  guaranteed   weathersit min 1  (removal: line 5: EXPECT days.weathersit BETWEEN 1 AND 4)
  guaranteed   weathersit max 4  (removal: line 5: EXPECT days.weathersit BETWEEN 1 AND 4)
  guaranteed   temp not_null  (removal: line 6: removed because the condition is true for every row of days)
  guaranteed   temp min 0  (removal: line 6: removed because the condition is true for every row of days)
composes: stage 2 never refuses or deoptimizes a stage-1 output for its values
```
