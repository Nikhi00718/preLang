#include "optimizer/ColumnPruningPass.h"

#include <algorithm>
#include <map>
#include <set>

namespace {

bool contains(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? ", " : "") + items[i];
    }
    return text;
}

// The instruction works on a table the compiler knows only from released facts.
bool releasedTable(const IRProgram& program, const Instruction& instruction) {
    const auto found = program.tables.find(tableKey(instruction.dataset, instruction.table));
    return found != program.tables.end() && found->second.released;
}

// The columns of the instruction's table after it runs, given those before.
void applyColumns(std::vector<std::string>& columns, const Instruction& instruction) {
    if (instruction.op == Opcode::Load || instruction.op == Opcode::Select) {
        columns = instruction.columns;
    } else if (instruction.op == Opcode::Derive && !contains(columns, instruction.column)) {
        columns.push_back(instruction.column);
    } else if (instruction.op == Opcode::Drop) {
        columns.erase(std::remove_if(columns.begin(), columns.end(),
                                     [&](const std::string& c) { return contains(instruction.columns, c); }),
                      columns.end());
    } else if (instruction.op == Opcode::Join) {
        columns.insert(columns.end(), instruction.columns.begin(), instruction.columns.end());
    } else if (instruction.op == Opcode::Aggregate) {
        columns = instruction.columns;
        for (const AggregateResult& item : instruction.aggregates) {
            columns.push_back(item.name);
        }
    }
}

// The columns each instruction's table has just before the instruction runs.
std::vector<std::vector<std::string>> columnsBefore(const IRProgram& program) {
    std::map<std::string, std::vector<std::string>> current;
    std::vector<std::vector<std::string>> result;
    for (const Instruction& instruction : program.code) {
        std::vector<std::string>& columns = current[tableKey(instruction.dataset, instruction.table)];
        result.push_back(columns);
        applyColumns(columns, instruction);
    }
    return result;
}

}  // namespace

void ColumnPruningPass::run(IRProgram& program, OptimizationReport& report) {
    const std::vector<std::vector<std::string>> before = columnsBefore(program);
    std::map<std::string, std::set<std::string>> live;  // table -> live columns
    std::vector<bool> remove(program.code.size(), false);
    std::vector<std::string> changes;  // found backwards, reported in program order

    for (size_t i = program.code.size(); i-- > 0;) {
        Instruction& instruction = program.code[i];
        std::set<std::string>& liveColumns = live[tableKey(instruction.dataset, instruction.table)];

        switch (instruction.op) {
            case Opcode::Save: liveColumns.insert(before[i].begin(), before[i].end()); break;
            case Opcode::DropNull:  // DROP_NULL checks its columns (all, by default) to decide which rows to remove
                if (instruction.columns.empty()) {
                    liveColumns.insert(before[i].begin(), before[i].end());
                } else {
                    liveColumns.insert(instruction.columns.begin(), instruction.columns.end());
                }
                break;

            case Opcode::Derive:  // defines its column from the columns it reads
                if (liveColumns.count(instruction.column) == 0) {
                    remove[i] = true;
                    changes.push_back("dead store elimination: removed " + describe(instruction) + ", '" +
                                      instruction.column + "' is never read or saved afterwards");
                    break;
                }
                liveColumns.erase(instruction.column);
                liveColumns.insert(instruction.columns.begin(), instruction.columns.end());
                break;

            case Opcode::FillNull:
            case Opcode::Normalize:
            case Opcode::ParseDate:
                if (liveColumns.count(instruction.column) == 0) {
                    remove[i] = true;
                    if (!instruction.proofKey.empty()) {
                        program.deadKeys.insert(instruction.proofKey);  // removable whatever the data
                    }
                    changes.push_back("dead store elimination: removed " + describe(instruction) + ", '" +
                                      instruction.column + "' is never read or saved afterwards");
                }
                break;  // if kept, it reads the column, which is already live

            case Opcode::Compare: liveColumns.insert(instruction.column); break;

            case Opcode::Check:
                // A speculatively removed check does not keep its column loaded: if the
                // column is pruned, the check is removed for good (see dropUnloadedChecks)
                // and its proof becomes part of the admission certificate. After a private
                // compilation it does: a proof from released facts must never decide whether
                // a batch is refused, because the analyst would see that (it could reveal a
                // row); the check stays able to run again, so a run ends as the source
                // program's run would, whatever the facts.
                if (instruction.speculative && !releasedTable(program, instruction)) {
                    break;
                }
                if (instruction.text == "MATCH_RATE") {  // reads the file-name column of the labels
                    live[instruction.columns[1]].insert(instruction.columns[2]);
                    break;
                }
                liveColumns.insert(instruction.column);
                if (instruction.text == "REFERENCES") {  // also reads the key column
                    live[tableKey(instruction.columns[0], instruction.columns[1])].insert(instruction.columns[2]);
                }
                break;

            case Opcode::Select: {  // before a SELECT, only selected columns that are live after it matter
                std::set<std::string> needed;
                for (const std::string& column : instruction.columns) {
                    if (liveColumns.count(column) != 0) {
                        needed.insert(column);
                    }
                }
                liveColumns = needed;
                break;
            }

            case Opcode::Drop:
                for (const std::string& column : instruction.columns) {
                    liveColumns.erase(column);
                }
                break;

            case Opcode::Join: {  // adds only the lookup columns read or saved afterwards; reads the key on both sides
                const std::string original = describe(instruction);
                std::vector<std::string> kept;
                std::vector<std::string> skipped;
                for (const std::string& column : instruction.columns) {
                    (liveColumns.count(column) != 0 ? kept : skipped).push_back(column);
                    liveColumns.erase(column);
                }
                if (!skipped.empty()) {
                    changes.push_back("column pruning: " + original + " adds only " +
                                      (kept.empty() ? std::string("the check of its key") : join(kept)) + ", skips " +
                                      join(skipped));
                }
                instruction.columns = kept;
                std::set<std::string>& lookup = live[tableKey(instruction.withDataset, instruction.withTable)];
                lookup.insert(kept.begin(), kept.end());
                lookup.insert(instruction.column);
                liveColumns.insert(instruction.column);
                break;
            }

            case Opcode::Aggregate: {  // reads its BY columns and the columns its results come from
                std::set<std::string> read(instruction.columns.begin(), instruction.columns.end());
                for (const AggregateResult& item : instruction.aggregates) {
                    if (!item.column.empty()) {
                        read.insert(item.column);
                    }
                }
                liveColumns = read;
                break;
            }

            case Opcode::Load: {
                std::vector<std::string> kept;
                std::vector<std::string> skipped;
                for (const std::string& column : instruction.columns) {
                    (liveColumns.count(column) != 0 ? kept : skipped).push_back(column);
                }
                if (!skipped.empty()) {
                    changes.push_back("column pruning: LOAD " + tableKey(instruction.dataset, instruction.table) +
                                      " (line " + std::to_string(instruction.line) + ") reads " +
                                      std::to_string(kept.size()) + " of " +
                                      std::to_string(instruction.columns.size()) + " columns, skips " + join(skipped));
                }
                instruction.columns = kept;
                break;
            }

            default:  // FILTER, AND, OR, NOT read only temporaries; image instructions have no columns
                break;
        }
    }
    report.changes.insert(report.changes.end(), changes.rbegin(), changes.rend());
    removeMarked(program, remove);
    fitColumnLists(program, report);
    dropUnloadedChecks(program, report);
}

// A speculatively removed CHECK of a column that is no longer loaded could not
// run if deoptimized, so it is removed for good: a new input must then keep its
// proof to be admitted at all.
void ColumnPruningPass::dropUnloadedChecks(IRProgram& program, OptimizationReport& report) {
    const std::vector<std::vector<std::string>> before = columnsBefore(program);
    std::vector<bool> remove(program.code.size(), false);
    for (size_t i = 0; i < program.code.size(); ++i) {
        const Instruction& instruction = program.code[i];
        if (instruction.op == Opcode::Check && instruction.speculative && !contains(before[i], instruction.column)) {
            remove[i] = true;
            report.changes.push_back("column pruning: " + describe(instruction) + " stays removed, '" +
                                     instruction.column + "' is not loaded (its proof is required on admission)");
        }
    }
    removeMarked(program, remove);
}

// After pruning, SELECT and DROP may name columns that are no longer loaded.
// Those names are removed, and a DROP left with no columns is removed too.
// The columns are followed in program order with each list already fitted: a
// SELECT that loses a column means a later DROP of it has nothing to drop.
void ColumnPruningPass::fitColumnLists(IRProgram& program, OptimizationReport& report) {
    std::map<std::string, std::vector<std::string>> current;
    std::vector<bool> remove(program.code.size(), false);

    for (size_t i = 0; i < program.code.size(); ++i) {
        Instruction& instruction = program.code[i];
        std::vector<std::string>& loaded = current[tableKey(instruction.dataset, instruction.table)];
        if (instruction.op == Opcode::Select || instruction.op == Opcode::Drop) {
            const std::string original = describe(instruction);
            std::vector<std::string>& columns = instruction.columns;
            const size_t count = columns.size();
            columns.erase(std::remove_if(columns.begin(), columns.end(),
                                         [&](const std::string& c) { return !contains(loaded, c); }),
                          columns.end());
            if (columns.size() != count && instruction.op == Opcode::Drop && columns.empty()) {
                remove[i] = true;
                report.changes.push_back("column pruning: removed " + original + ", its columns are never loaded");
            } else if (columns.size() != count) {
                report.changes.push_back("column pruning: " + original + " now lists only loaded columns");
            }
        }
        applyColumns(loaded, instruction);
    }
    removeMarked(program, remove);
}
