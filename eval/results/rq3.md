# RQ3: what the optimizations buy

## Tables: column pruning and contract-check elimination (200,000 rows x 40 columns)

| Variant | Columns read | Run-time checks | Time (s) |
|---|---:|---:|---:|
| PrepLang, optimized | 5 | 0 | 1.30 |
| PrepLang, --O0 | 40 | 2 | 1.54 |
| Script-style baseline | 40 | 2 | 1.47 |

Speedup of the optimizer: 1.19x; optimized and --O0 outputs identical: True

## Images: one streaming pass vs. loading everything (1,000 images, 640x480)

| Variant | Time (s) | Peak memory (MiB) |
|---|---:|---:|
| PrepLang (streaming) | 3.24 | 10 |
| Script-style baseline (all in memory) | 3.54 | 922 |

Same pixels in a sample of 20 outputs: True

## Approximate RESIZE fusion (512x512 then 224x224, 1,000 images)

- exact: 5.99 s, --approx: 5.64 s, speedup 1.06x
- PSNR of approximate vs. exact output: mean 57.0 dB, minimum 51.8 dB

# RQ4: cost of compiling (reading the data at compile time)

| Input | Compile time (s) | One read in Python (s): pandas.read_csv, or cv2.imread of every image |
|---|---:|---:|
| CSV, 10,000 rows x 10 columns | 0.03 | 0.03 |
| CSV, 100,000 rows x 10 columns | 0.19 | 0.10 |
| CSV, 1,000,000 rows x 10 columns | 1.75 | 0.77 |
| 100 JPG images, 640x480 | 0.02 | 0.16 |
| 1000 JPG images, 640x480 | 0.07 | 1.44 |
| 3000 JPG images, 640x480 | 0.17 | 4.31 |
