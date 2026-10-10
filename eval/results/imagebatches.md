# Reusing a compiled image pipeline on new folders

| New folder | Folders | Full certificate | Deoptimization | Trusted optimized program | Removals re-enabled |
|---|---:|---|---|---|---|
| gray 64x64 | 5 | 5 correct run | 5 correct run | 5 correct run | - |
| some color | 5 | 5 needless stop | 5 correct run | 5 unsound | 5 x line 3: removed because every image already has 1 channel |
| other sizes | 5 | 5 needless stop | 5 correct run | 5 unsound | 5 x line 4: removed because every image is already 64x64 |
| color and sizes | 5 | 5 needless stop | 5 correct run | 5 unsound | 5 x line 3: removed because every image already has 1 channel; 5 x line 4: removed because every image is already 64x64 |
| a broken file | 5 | 5 correct stop | 5 correct stop | 5 correct stop | - |
| 16-bit | 5 | 5 needless stop | 5 needless stop | 5 correct run | - |
