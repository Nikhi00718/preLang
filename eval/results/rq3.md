# RQ3: what the optimizations buy

## Tables: column pruning and contract-check elimination (200,000 rows x 40 columns)

| Variant | Columns read | Run-time checks | Time (s) |
|---|---:|---:|---:|
| PrepLang, optimized | 5 | 0 | 1.49 |
| PrepLang, --O0 | 40 | 2 | 1.81 |
| Script-style baseline | 40 | 2 | 1.82 |

Speedup of the optimizer: 1.21x; optimized and --O0 outputs identical: True

## Images: one streaming pass vs. loading everything (1,000 images, 640x480)

| Variant | Time (s) | Peak memory (MiB) |
|---|---:|---:|
| PrepLang (streaming) | 3.52 | 10 |
| Script-style baseline (all in memory) | 4.01 | 922 |
| Hand-written streaming script (eval/handwritten.py) | 3.54 | 12 |

Same pixels in a sample of 20 outputs: True

## Approximate RESIZE fusion (512x512 then 224x224, 1,000 images)

- exact: 5.56 s, --approx: 5.26 s, speedup 1.06x
- PSNR of approximate vs. exact output: mean 57.0 dB, minimum 51.8 dB

# RQ4: cost of compiling (reading the data at compile time)

| Input | Compile time (s) | One read in Python (s): pandas.read_csv, or cv2.imread of every image |
|---|---:|---:|
| CSV, 10,000 rows x 10 columns | 0.01 | 0.06 |
| CSV, 100,000 rows x 10 columns | 0.03 | 0.08 |
| CSV, 1,000,000 rows x 10 columns | 0.13 | 0.62 |
| 100 JPG images, 640x480 | 0.02 | 0.14 |
| 1000 JPG images, 640x480 | 0.08 | 1.48 |
| 3000 JPG images, 640x480 | 0.33 | 5.73 |
