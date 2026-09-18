#pragma once

#include "dataset/LineageTracker.h"

#include <ostream>

// --lineage: the history of every column and image set, e.g.
//   students.age
//     |-- line 2: loaded from examples/students.csv      INTEGER? [19, 23]
//     |-- line 9: DROP_NULL                              INTEGER [19, 23]
//     `-- line 21: saved to "clean.csv"                  INTEGER [19, 23]
void printLineage(std::ostream& out, const LineageTracker& lineage);
