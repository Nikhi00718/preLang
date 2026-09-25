"""Writes the small Parquet files used by tests/test_parquet.cpp.

    py -3 tests/data/make_parquet.py

The files are committed, so the C++ tests do not need Python or pyarrow.
"""
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq

HERE = Path(__file__).resolve().parent

students = pa.table({
    "name": pa.array(["Asha", "Ravi", "Mina", "Joe", "Lee", "Tara"], pa.string()),
    "age": pa.array([20, None, 22, 21, 23, 19], pa.int64()),
    "marks": pa.array([85, 42, 91, 60, 77, 35], pa.int64()),
    "score": pa.array([0.5, 0.25, 1.0, 0.75, 0.1, 0.9], pa.float64()),
    "passed": pa.array([True, False, True, True, True, False], pa.bool_()),
    "id": pa.array([1, 2, 3, 4, 5, 5], pa.int64()),
})
# Three row groups of two rows, so the statistics must be combined.
pq.write_table(students, HERE / "students.parquet", row_group_size=2)
# The same data without statistics: nothing is known about values or missing values.
pq.write_table(students, HERE / "nostats.parquet", write_statistics=False)

# A REQUIRED (non-nullable) unsigned column, without statistics.
required = pa.table({"n": pa.array([1, 4_000_000_000], pa.uint32())},
                    schema=pa.schema([pa.field("n", pa.uint32(), nullable=False)]))
pq.write_table(required, HERE / "required.parquet")

pq.write_table(pa.table({"day": pa.array([0, 1], pa.date32())}), HERE / "dates.parquet")
pq.write_table(pa.table({"tags": pa.array([["a"], ["b", "c"]])}), HERE / "nested.parquet")

# Magic numbers and a footer length, but the footer is five zero bytes.
(HERE / "broken.parquet").write_bytes(b"PAR1" + bytes(10) + (5).to_bytes(4, "little") + b"PAR1")
print("wrote", HERE)
