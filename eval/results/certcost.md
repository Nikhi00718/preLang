# Certification cost at compile time

| Program | Removals | Re-analyses (deopt) | Compile s (deopt) | Re-analyses (--no-deopt) | Compile s (--no-deopt) |
|---|---:|---:|---:|---:|---:|
| imageset | 0 | 16 | 0.026 | 16 | 0.019 |
| linked | 0 | 18 | 0.017 | 18 | 0.026 |
| optimize_images | 2 | 14 | 0.026 | 14 | 0.026 |
| join_aggregate | 0 | 0 | 0.013 | 0 | 0.013 |
| metadata_only | 2 | 40 | 0.025 | 31 | 0.027 |
| ml_split | 1 | 27 | 0.029 | 24 | 0.029 |
| optimize | 3 | 67 | 0.049 | 55 | 0.043 |
| students | 2 | 23 | 0.025 | 14 | 0.022 |
| tableset | 0 | 58 | 0.293 | 58 | 0.292 |
| traffic | 6 | 146 | 0.089 | 63 | 0.046 |
| bikes | 6 | 207 | 0.165 | 122 | 0.112 |
| taxi | 4 | 162 | 0.265 | 119 | 0.210 |
