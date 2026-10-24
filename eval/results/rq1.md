# RQ1: when is each injected mistake found?

- Mutants: 24; controls (original programs): 5
- PrepLang reports 24/24 before any data is processed (23 as errors, 1 as warnings); false alarms on controls: 0
- Script-style baseline: 20 crash while processing, 4 finish with silently wrong output

| ID | Class | Injected mistake | PrepLang (compile time) | Baseline (run time) | PrepLang ms | Baseline ms |
|---|---|---|---|---|---:|---:|
| C-students | control | original program | not reported | completed | 82 | 96 |
| C-company | control | original program | not reported | completed | 381 | 31 |
| C-split | control | original program | not reported | completed | 54 | 7 |
| C-images | control | original program | not reported | completed | 88 | 53 |
| C-linked | control | original program | not reported | completed | 41 | 24 |
| T1 | name | misspelled column | compile error (semantic) | run-time crash (KeyError) | 36 | 4 |
| T2 | type | NORMALIZE on a text column | compile error (type) | run-time crash (TypeError) | 41 | 14 |
| T3 | type | number compared with text | compile error (type) | run-time crash (TypeError) | 38 | 3 |
| T4 | type | text filled into an INTEGER column | compile error (type) | silent wrong output | 41 | 9 |
| T5 | lifecycle | column used after SELECT removed it | compile error (semantic) | run-time crash (KeyError) | 38 | 7 |
| T6 | contract | range contract broken by the data | compile error (contract) | run-time crash (AssertionError) | 44 | 2 |
| T7 | contract | NOT_NULL on a column with missing values | compile error (contract) | run-time crash (AssertionError) | 43 | 4 |
| T8 | value | threshold on a normalized column | compile warning | silent wrong output | 57 | 7 |
| S1 | name | wildcard column missing in some tables | compile error (semantic) | run-time crash (KeyError) | 43 | 6 |
| S2 | type | NORMALIZE on text columns of every table | compile error (type) | run-time crash (TypeError) | 66 | 8 |
| S3 | contract | foreign key into the wrong table | compile error (contract) | run-time crash (AssertionError) | 46 | 6 |
| S4 | lifecycle | FILTER on a whole TABLESET | compile error (semantic) | run-time crash (KeyError) | 39 | 8 |
| S5 | contract | range contract across tables | compile error (contract) | run-time crash (AssertionError) | 42 | 6 |
| M1 | contract | train and test schemas differ | compile error (contract) | run-time crash (AssertionError) | 34 | 2 |
| M2 | name | misspelled column in a contract | compile error (semantic) | run-time crash (KeyError) | 35 | 3 |
| I1 | validity | broken file not removed | compile error (schema) | run-time crash (AttributeError) | 38 | 7 |
| I2 | contract | channel contract broken by a gray image | compile error (contract) | run-time crash (AssertionError) | 33 | 7 |
| I3 | contract | minimum width broken by one image | compile error (contract) | run-time crash (AssertionError) | 24 | 7 |
| I4 | lifecycle | table operation on images | compile error (type) | run-time crash (TypeError) | 36 | 9 |
| I5 | value | float pixels saved as JPG | compile error (type) | silent wrong output | 37 | 22 |
| I6 | type | zero width in RESIZE | compile error (semantic) | run-time crash (error) | 42 | 10 |
| L1 | contract | labels file misses an image | compile error (contract) | run-time crash (AssertionError) | 50 | 9 |
| L2 | value | float pixels saved as PNG | compile error (type) | silent wrong output | 36 | 19 |
| L3 | contract | images linked on the wrong column | compile error (contract) | run-time crash (AssertionError) | 31 | 10 |

# RQ2: contracts decided at compile time (controls)

- proven statically: 15, left for run time: 1, decided statically: 94%
