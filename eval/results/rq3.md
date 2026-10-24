# RQ3: what the optimizations buy

## Tables: column pruning and contract-check elimination (200,000 rows x 40 columns)

| Variant | Columns read | Run-time checks | Time (s) |
|---|---:|---:|---:|
| PrepLang, optimized | 5 | 0 | 2.32 |
| PrepLang, --O0 | 40 | 2 | 2.83 |
| Script-style baseline | 40 | 2 | 2.49 |

Speedup of the optimizer: 1.22x; optimized and --O0 outputs identical: True

## Images: one streaming pass vs. loading everything (1,000 images, 640x480)

| Variant | Time (s) | Peak memory (MiB) |
|---|---:|---:|
| PrepLang (streaming) | 4.34 | 10 |
| Script-style baseline (all in memory) | 5.36 | 922 |
| Hand-written streaming script (eval/compilation/handwritten.py) | 4.01 | 12 |

Same pixels in a sample of 20 outputs: True

## Approximate RESIZE fusion (512x512 then 224x224, 1,000 images)

- exact: 6.22 s, --approx: 5.98 s, speedup 1.04x
- PSNR of approximate vs. exact output: mean 57.0 dB, minimum 51.8 dB

# RQ4: cost of compiling (reading the data at compile time)

| Input | Compile time (s) | One read in Python (s): pandas.read_csv, or cv2.imread of every image |
|---|---:|---:|
| CSV, 10,000 rows x 10 columns | 0.06 | 0.07 |
| CSV, 100,000 rows x 10 columns | 0.06 | 0.10 |
| CSV, 1,000,000 rows x 10 columns | 0.22 | 0.76 |
| 100 JPG images, 640x480 | 0.04 | 0.14 |
| 1000 JPG images, 640x480 | 0.12 | 1.27 |
| 3000 JPG images, 640x480 | 0.38 | 4.56 |
