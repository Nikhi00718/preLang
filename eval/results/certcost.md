# Certification cost at compile time

| Program | Removals | Re-analyses (deopt) | Compile s (deopt) | Re-analyses (--no-deopt) | Compile s (--no-deopt) |
|---|---:|---:|---:|---:|---:|
| imageset | 0 | 16 | 0.021 | 16 | 0.023 |
| linked | 0 | 18 | 0.024 | 18 | 0.024 |
| optimize_images | 2 | 14 | 0.021 | 14 | 0.021 |
| metadata_only | 2 | 40 | 0.021 | 31 | 0.022 |
| ml_split | 1 | 27 | 0.022 | 24 | 0.023 |
| optimize | 3 | 67 | 0.028 | 55 | 0.024 |
| students | 2 | 23 | 0.024 | 14 | 0.024 |
| tableset | 0 | 58 | 0.032 | 58 | 0.035 |
| traffic | 6 | 146 | 0.030 | 63 | 0.026 |
| bikes | 6 | 207 | 0.034 | 122 | 0.027 |
| taxi | 4 | 162 | 0.032 | 119 | 0.032 |
