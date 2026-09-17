#include "optimizer/Optimizer.h"

#include "ir/IRBuilder.h"

#include <algorithm>
#include <map>

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

// "`DROP_NULL s` (line 4)"
std::string describe(const Instruction& instruction) {
    return "`" + instructionText(instruction) + "` (line " + std::to_string(instruction.line) + ")";
}

// The columns each instruction's dataset has just before the instruction runs.
std::vector<std::vector<std::string>> columnsBefore(const IRProgram& program) {
    std::map<std::string, std::vector<std::string>> current;
    std::vector<std::vector<std::string>> result;
    for (const Instruction& instruction : program.code) {
        std::vector<std::string>& columns = current[instruction.dataset];
        result.push_back(columns);
        if (instruction.op == Opcode::Load || instruction.op == Opcode::Select) {
            columns = instruction.columns;
        } else if (instruction.op == Opcode::Drop) {
            columns.erase(std::remove_if(columns.begin(), columns.end(),
                                         [&](const std::string& c) { return contains(instruction.columns, c); }),
                          columns.end());
        }
    }
    return result;
}

}  // namespace

OptimizationReport Optimizer::optimize(IRProgram& program) {
    report_ = OptimizationReport{};
    report_.instructionsBefore = static_cast<int>(program.code.size());

    removeNoEffectInstructions(program);
    removeDeadTemporaries(program);
    removeDeadStoresAndPruneColumns(program);
    fitColumnLists(program);

    report_.instructionsAfter = static_cast<int>(program.code.size());
    return report_;
}

// Pass 1: semantic analysis already proved these instructions change nothing.
void Optimizer::removeNoEffectInstructions(IRProgram& program) {
    std::vector<bool> remove(program.code.size(), false);
    for (size_t i = 0; i < program.code.size(); ++i) {
        const Instruction& instruction = program.code[i];
        if (instruction.noEffect.empty()) {
            continue;
        }
        remove[i] = true;
        const std::string pass =
            instruction.op == Opcode::Check ? "contract check elimination" : "no-effect elimination";
        report_.changes.push_back(pass + ": removed " + describe(instruction) + ", " + instruction.noEffect);
    }
    removeMarked(program, remove);
}

// Pass 2: walking backwards, a temporary is dead if no later kept instruction
// reads it. Removing a FILTER in pass 1 makes its whole condition dead here.
void Optimizer::removeDeadTemporaries(IRProgram& program) {
    std::vector<bool> remove(program.code.size(), false);
    std::vector<std::string> changes;  // found backwards, reported in program order
    std::set<int> used;
    for (size_t i = program.code.size(); i-- > 0;) {
        const Instruction& instruction = program.code[i];
        if (instruction.result != 0 && used.count(instruction.result) == 0) {
            remove[i] = true;
            changes.push_back("dead code elimination: removed " + describe(instruction) + ", t" +
                              std::to_string(instruction.result) + " is never used");
            continue;
        }
        used.insert(instruction.uses.begin(), instruction.uses.end());
    }
    report_.changes.insert(report_.changes.end(), changes.rbegin(), changes.rend());
    removeMarked(program, remove);
}

// Passes 3 and 4: backward liveness analysis where the "variables" are columns.
// A column is live if its current values may still be read or saved later.
void Optimizer::removeDeadStoresAndPruneColumns(IRProgram& program) {
    const std::vector<std::vector<std::string>> before = columnsBefore(program);
    std::map<std::string, std::set<std::string>> live;  // dataset -> live columns
    std::vector<bool> remove(program.code.size(), false);
    std::vector<std::string> changes;  // found backwards, reported in program order

    for (size_t i = program.code.size(); i-- > 0;) {
        Instruction& instruction = program.code[i];
        std::set<std::string>& liveColumns = live[instruction.dataset];

        switch (instruction.op) {
            case Opcode::Save:
            case Opcode::DropNull:  // DROP_NULL checks every column to decide which rows to remove
                liveColumns.insert(before[i].begin(), before[i].end());
                break;

            case Opcode::FillNull:
            case Opcode::Normalize:
                if (liveColumns.count(instruction.column) == 0) {
                    remove[i] = true;
                    changes.push_back("dead store elimination: removed " + describe(instruction) + ", '" +
                                      instruction.column + "' is never read or saved afterwards");
                }
                break;  // if kept, it reads the column, which is already live

            case Opcode::Check:
            case Opcode::Compare:
                liveColumns.insert(instruction.column);
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

            case Opcode::Load: {
                std::vector<std::string> kept;
                std::vector<std::string> skipped;
                for (const std::string& column : instruction.columns) {
                    (liveColumns.count(column) != 0 ? kept : skipped).push_back(column);
                }
                changes.push_back("column pruning: LOAD " + instruction.dataset + " (line " +
                                  std::to_string(instruction.line) + ") reads " + std::to_string(kept.size()) +
                                  " of " + std::to_string(instruction.columns.size()) + " columns" +
                                  (skipped.empty() ? "" : ", skips " + join(skipped)));
                instruction.columns = kept;
                break;
            }

            case Opcode::Filter:  // FILTER, AND, OR and NOT only read temporaries
            case Opcode::And:
            case Opcode::Or:
            case Opcode::Not:
                break;
        }
    }
    report_.changes.insert(report_.changes.end(), changes.rbegin(), changes.rend());
    removeMarked(program, remove);
}

// After pruning, SELECT and DROP may name columns that are no longer loaded.
// Those names are removed, and a DROP left with no columns is removed too.
void Optimizer::fitColumnLists(IRProgram& program) {
    const std::vector<std::vector<std::string>> before = columnsBefore(program);
    std::vector<bool> remove(program.code.size(), false);

    for (size_t i = 0; i < program.code.size(); ++i) {
        Instruction& instruction = program.code[i];
        if (instruction.op != Opcode::Select && instruction.op != Opcode::Drop) {
            continue;
        }
        const std::string original = describe(instruction);
        std::vector<std::string>& columns = instruction.columns;
        const size_t count = columns.size();
        columns.erase(std::remove_if(columns.begin(), columns.end(),
                                     [&](const std::string& c) { return !contains(before[i], c); }),
                      columns.end());
        if (columns.size() == count) {
            continue;
        }
        if (instruction.op == Opcode::Drop && columns.empty()) {
            remove[i] = true;
            report_.changes.push_back("column pruning: removed " + original + ", its columns are never loaded");
        } else {
            report_.changes.push_back("column pruning: " + original + " now lists only loaded columns");
        }
    }
    removeMarked(program, remove);
}

void Optimizer::removeMarked(IRProgram& program, const std::vector<bool>& remove) {
    std::vector<Instruction> kept;
    for (size_t i = 0; i < program.code.size(); ++i) {
        if (!remove[i]) {
            kept.push_back(std::move(program.code[i]));
        }
    }
    program.code = std::move(kept);
}

void printReport(std::ostream& out, const OptimizationReport& report) {
    for (const std::string& change : report.changes) {
        out << "- " << change << '\n';
    }
    out << "Instructions: " << report.instructionsBefore << " -> " << report.instructionsAfter << '\n';
}
