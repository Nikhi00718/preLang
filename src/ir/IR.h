#pragma once

#include "ast/AST.h"
#include "schema/ImageSchema.h"
#include "schema/TableSchema.h"
#include "sema/AnalysisResult.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// Intermediate representation: one flat list of simple instructions for both
// tables and images. Wildcards are already expanded (company.*.age becomes one
// instruction per table), and FILTER conditions become three-address code:
//   t1 = s.marks >= 50;  t2 = s.passed == TRUE;  t3 = t1 AND t2;  FILTER s IF t3
enum class Opcode {
    Load,
    Check,
    FillNull,
    DropNull,
    Normalize,
    Compare,
    And,
    Or,
    Not,
    Filter,
    Select,
    Drop,
    Save,
    ParseDate,
    Derive,
    Join,
    Aggregate,
    LoadImages,
    Resize,
    Grayscale,
    NormalizeImages,
    Compress,
    Convert,
    RemoveCorrupt,
    SaveImages,
};

// One instruction: a quadruple (operation, arguments, result) with extra slots
// for tables, column lists and file names. The slots each opcode uses:
//
//   LOAD          dataset, table, columns = columns to read, text = file
//   CHECK         dataset, table, column, text = NOT_NULL | BETWEEN | UNIQUE | REFERENCES | MATCH_RATE,
//                 values = [low, high] (BETWEEN) or [percent] (MATCH_RATE),
//                 columns = [dataset, table, column] of the key (REFERENCES)
//                           or [images, labels, column, (SKIP_BROKEN)] (MATCH_RATE)
//   FILL_NULL     dataset, table, column, values = [fill value], or text = MEAN | MEDIAN | MODE
//   DROP_NULL     dataset, table, columns = the columns to look at (empty: all)
//   NORMALIZE     dataset, table, column, text = ZSCORE | MINMAX
//   COMPARE       dataset, table, column, text = operator, values = [value], result
//   AND, OR       result, uses = [left, right]
//   NOT           result, uses = [operand]
//   FILTER        dataset, table, uses = [condition]
//   SELECT, DROP  dataset, table, columns
//   SAVE          dataset, table, text = file
//   PARSE_DATE    dataset, table, column
//   DERIVE        dataset, table, column = the new column, columns = the columns it reads,
//                 text = the value as written, value = its tree
//   JOIN          dataset, table, column = the key, withDataset, withTable = the lookup table,
//                 columns = the columns it adds
//   AGGREGATE     dataset, table, columns = the BY columns, aggregates = the new columns
//   LOAD_IMAGES   dataset, text = folder or file
//   RESIZE        dataset, numbers = [width, height, keep aspect (0 or 1)]
//   GRAYSCALE     dataset
//   NORMALIZE_IMAGES  dataset, values = [low, high, source low, source high]
//   COMPRESS      dataset, numbers = [quality]
//   CONVERT       dataset, text = JPG | PNG | NPY
//   REMOVE_CORRUPT    dataset
//   SAVE_IMAGES   dataset, text = folder (or file), numbers = [one file (0 or 1)]
// One new column of AGGREGATE, with the type analysis gave it.
struct AggregateResult {
    std::string function;  // COUNT, SUM, MEAN, MIN or MAX
    std::string column;    // empty for COUNT
    std::string name;
    std::string type;  // INTEGER or FLOAT
};

struct Instruction {
    explicit Instruction(Opcode opcode) : op(opcode) {}

    Opcode op;
    std::string dataset;
    std::string table;  // table inside a TABLESET; empty for a TABLE and for images
    std::string column;
    std::vector<std::string> columns;
    std::string text;
    std::vector<Literal> values;
    std::shared_ptr<const ValueExpr> value;  // DERIVE
    std::string withDataset;                 // JOIN: the lookup table
    std::string withTable;
    std::vector<AggregateResult> aggregates;  // AGGREGATE
    std::vector<int> numbers;
    int result = 0;         // temporary t<result> this instruction defines; 0 if none
    std::vector<int> uses;  // temporaries this instruction reads
    int line = 0;           // source line it was generated from
    std::string noEffect;   // why analysis proved it changes nothing; empty if it may change data
    // A CHECK proven from the values of these tables. The optimizer keeps it, but it
    // runs only if one of them is not the file the compiler read (see certify/).
    std::vector<std::string> provenFromValuesOf;
    bool onlyIfInputChanged = false;
    // proofKey of the analysis fact that justifies removing it (see sema/AnalysisResult.h).
    std::string proofKey;
    // Removed speculatively: the generated program skips it, unless a new input
    // breaks the item certificate of proofKey; then it runs after all (deoptimization).
    bool speculative = false;
};

struct IRProgram {
    std::vector<Instruction> code;
    std::map<std::string, TableSchema> tables;     // tableKey -> the CSV file a LOAD reads
    std::map<std::string, ImageSetSchema> images;  // dataset -> the files a LOAD_IMAGE(S) reads
    // proofKeys of instructions removed as dead stores (their result is never used). That
    // removal holds for every input, so a certificate does not need the data fact that also
    // made them useless.
    std::set<std::string> deadKeys;
};
