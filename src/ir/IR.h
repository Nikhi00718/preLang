#pragma once

#include "ast/AST.h"
#include "schema/Schema.h"

#include <map>
#include <string>
#include <vector>

// Intermediate representation: a flat list of simple instructions. FILTER
// conditions become three-address code, where every step stores its result
// in a temporary:  t1 = s.marks >= 50;  t2 = s.passed == TRUE;  t3 = t1 AND t2
enum class Opcode { Load, Check, FillNull, DropNull, Normalize, Compare, And, Or, Not, Filter, Select, Drop, Save };

// One instruction: a quadruple (operation, arguments, result) with extra slots
// for column lists and file names. The slots each opcode uses:
//
//   LOAD       dataset, columns = columns to read, text = file
//   CHECK      dataset, column, text = NOT_NULL | BETWEEN, values = [low, high]
//   FILL_NULL  dataset, column, values = [fill value]
//   DROP_NULL  dataset
//   NORMALIZE  dataset, column, text = ZSCORE | MINMAX
//   COMPARE    dataset, column, text = operator, values = [value], result
//   AND, OR    result, uses = [left, right]
//   NOT        result, uses = [operand]
//   FILTER     dataset, uses = [condition]
//   SELECT     dataset, columns
//   DROP       dataset, columns
//   SAVE       dataset, text = file
struct Instruction {
    explicit Instruction(Opcode opcode) : op(opcode) {}

    Opcode op;
    std::string dataset;
    std::string column;
    std::vector<std::string> columns;
    std::string text;
    std::vector<Literal> values;
    int result = 0;         // temporary t<result> this instruction defines; 0 if none
    std::vector<int> uses;  // temporaries this instruction reads
    int line = 0;           // source line it was generated from
    std::string noEffect;   // why analysis proved it changes nothing; empty if it may change data
};

struct IRProgram {
    std::vector<Instruction> code;
    std::map<std::string, Schema> inputs;  // dataset -> schema of the CSV its LOAD reads
};
