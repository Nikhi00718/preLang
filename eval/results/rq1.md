# RQ1: when is each injected mistake found?

- Mutants: 24; controls (original programs): 5
- PrepLang reports 24/24 before any data is processed (23 as errors, 1 as warnings); false alarms on controls: 0
- Script-style baseline: 20 crash while processing, 4 finish with silently wrong output

| ID | Class | Injected mistake | PrepLang (compile time) | Baseline (run time) | PrepLang ms | Baseline ms |
|---|---|---|---|---|---:|---:|
| C-students | control | original program | not reported | completed | 23 | 18 |
| C-company | control | original program | not reported | completed | 30 | 13 |
| C-split | control | original program | not reported | completed | 16 | 7 |
| C-images | control | original program | not reported | completed | 17 | 13 |
| C-linked | control | original program | not reported | completed | 18 | 18 |
| T1 | name | misspelled column | compile error (semantic) | run-time crash (KeyError) | 19 | 2 |
| T2 | type | NORMALIZE on a text column | compile error (type) | run-time crash (TypeError) | 17 | 3 |
| T3 | type | number compared with text | compile error (type) | run-time crash (TypeError) | 19 | 2 |
| T4 | type | text filled into an INTEGER column | compile error (type) | silent wrong output | 19 | 5 |
| T5 | lifecycle | column used after SELECT removed it | compile error (semantic) | run-time crash (KeyError) | 18 | 5 |
| T6 | contract | range contract broken by the data | compile error (contract) | run-time crash (AssertionError) | 20 | 2 |
| T7 | contract | NOT_NULL on a column with missing values | compile error (contract) | run-time crash (AssertionError) | 17 | 2 |
| T8 | value | threshold on a normalized column | compile warning | silent wrong output | 17 | 5 |
| S1 | name | wildcard column missing in some tables | compile error (semantic) | run-time crash (KeyError) | 18 | 5 |
| S2 | type | NORMALIZE on text columns of every table | compile error (type) | run-time crash (TypeError) | 16 | 5 |
| S3 | contract | foreign key into the wrong table | compile error (contract) | run-time crash (AssertionError) | 18 | 4 |
| S4 | lifecycle | FILTER on a whole TABLESET | compile error (semantic) | run-time crash (KeyError) | 19 | 7 |
| S5 | contract | range contract across tables | compile error (contract) | run-time crash (AssertionError) | 21 | 4 |
| M1 | contract | train and test schemas differ | compile error (contract) | run-time crash (AssertionError) | 15 | 2 |
| M2 | name | misspelled column in a contract | compile error (semantic) | run-time crash (KeyError) | 16 | 2 |
| I1 | validity | broken file not removed | compile error (schema) | run-time crash (AttributeError) | 18 | 6 |
| I2 | contract | channel contract broken by a gray image | compile error (contract) | run-time crash (AssertionError) | 20 | 6 |
| I3 | contract | minimum width broken by one image | compile error (contract) | run-time crash (AssertionError) | 18 | 6 |
| I4 | lifecycle | table operation on images | compile error (type) | run-time crash (TypeError) | 17 | 7 |
| I5 | value | float pixels saved as JPG | compile error (type) | silent wrong output | 17 | 14 |
| I6 | type | zero width in RESIZE | compile error (semantic) | run-time crash (error) | 16 | 6 |
| L1 | contract | labels file misses an image | compile error (contract) | run-time crash (AssertionError) | 17 | 7 |
| L2 | value | float pixels saved as PNG | compile error (type) | silent wrong output | 17 | 16 |
| L3 | contract | images linked on the wrong column | compile error (contract) | run-time crash (AssertionError) | 20 | 7 |

# RQ2: contracts decided at compile time (controls)

- proven statically: 15, left for run time: 1, decided statically: 94%
