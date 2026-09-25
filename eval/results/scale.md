# RQ5: large inputs

## Compile time against a Parquet file (only the footer is read)

| Rows | File size (MiB) | Compile time (s) | pandas.read_parquet (s) |
|---:|---:|---:|---:|
| 1,000,000 | 22 | 0.02 | 0.66 |
| 4,000,000 | 87 | 0.02 | 0.19 |
| 16,000,000 | 346 | 0.02 | 0.84 |

## Time until a mistake is reported

Tables: 16,000,000 rows (Parquet). Images: 3,000 JPG files of 640x480. Times are medians of 3 runs and include starting the process.

| Case | PrepLang compile (s) | PrepLang | PrepLang run (s) | Script (s) | Script |
|---|---:|---|---:|---:|---|
| tables: correct program | 0.02 | compiled | 4.74 | 4.51 | finished |
| tables: misspelled column in the last step | 0.02 | compile error: column 'amont' does not exist in 'events' | - | 2.81 | crashed: KeyError: "['amont'] not in index" |
| tables: range contract broken by the data | 0.02 | compile error: contract violated: events.amount has values from 0 to 999, according to the statistics in  | - | 1.79 | crashed: AssertionError: amount out of range |
| images: correct program | 0.33 | compiled | 10.51 | 8.53 | finished |
| images: broken file (sorted last) not removed | 0.26 | compile error: 1 of 3001 files in C:/Users/atnik/Documents/ChatGPT/comp_v1/PrepLang/eval/data/images_3000 | - | 9.15 | crashed: cv2.error: OpenCV(5.0.0) D:\a\opencv-python\opencv-python\opencv\modules\imgproc\src\resiz |
| images: float pixels saved as PNG | 0.26 | compile error: cannot save FLOAT32 pixels as PNG: NORMALIZE at line 4 scaled them to [0, 1] | - | 8.54 | finished, but every image is black |
