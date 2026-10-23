#include "optimizer/RedundantOpPass.h"

#include <map>

RedundantOpPass::RedundantOpPass(bool approximate, bool speculate) : approximate_(approximate), speculate_(speculate) {}

void RedundantOpPass::run(IRProgram& program, OptimizationReport& report) {
    std::map<int, size_t> definedAt;  // temporary -> the instruction that computes it
    for (size_t i = 0; i < program.code.size(); ++i) {
        if (program.code[i].result != 0) {
            definedAt[program.code[i].result] = i;
        }
    }
    std::vector<bool> remove(program.code.size(), false);
    for (size_t i = 0; i < program.code.size(); ++i) {
        Instruction& instruction = program.code[i];
        if (instruction.noEffect.empty()) {
            continue;
        }
        // A proof that read the values holds only for those files. The check stays,
        // and runs only if the program is given a different file (data certificate).
        if (!instruction.provenFromValuesOf.empty()) {
            report.changes.push_back("contract check made conditional: " + describe(instruction) + " runs only if " +
                                     "an input differs from the file it was proven on, " + instruction.noEffect);
            instruction.onlyIfInputChanged = true;
            continue;
        }
        const std::string kind =
            instruction.op == Opcode::Check ? "contract check elimination" : "no-effect elimination";
        if (!speculate_) {
            remove[i] = true;
            report.changes.push_back(kind + ": removed " + describe(instruction) + ", " + instruction.noEffect);
            continue;
        }
        // Kept for deoptimization, with the condition a FILTER needs: the temporaries
        // of one statement belong to it alone, so they are skipped and re-run with it.
        instruction.speculative = true;
        std::vector<int> pending = instruction.uses;
        while (!pending.empty()) {
            const int temporary = pending.back();
            pending.pop_back();
            Instruction& condition = program.code[definedAt.at(temporary)];
            condition.speculative = true;
            condition.proofKey = instruction.proofKey;
            pending.insert(pending.end(), condition.uses.begin(), condition.uses.end());
        }
        report.changes.push_back(kind + ": removed " + describe(instruction) + ", " + instruction.noEffect +
                                 " (runs again if a new input breaks its certificate)");
    }
    removeMarked(program, remove);
    mergeResizes(program, report);
}

// RESIZE A followed by RESIZE B (with only encoding settings in between) gives
// almost the same pixels as RESIZE B alone. Only stretching resizes qualify:
// KEEP_ASPECT adds padding, which a later resize would keep.
void RedundantOpPass::mergeResizes(IRProgram& program, OptimizationReport& report) {
    std::vector<bool> remove(program.code.size(), false);
    for (size_t i = 0; i < program.code.size(); ++i) {
        const Instruction& first = program.code[i];
        if (first.op != Opcode::Resize || first.numbers[2] == 1 || first.speculative) {
            continue;
        }
        for (size_t j = i + 1; j < program.code.size(); ++j) {
            const Instruction& next = program.code[j];
            if (next.dataset != first.dataset || next.op == Opcode::Compress || next.op == Opcode::Convert ||
                next.speculative) {
                continue;
            }
            if (next.op == Opcode::Resize && next.numbers[2] == 0) {
                if (approximate_) {
                    remove[i] = true;
                    report.changes.push_back("RESIZE fusion (approximate): removed " + describe(first) + ", " +
                                             describe(next) + " alone gives almost the same pixels");
                } else {
                    report.suggestions.push_back("RESIZE fusion: removing " + describe(first) +
                                                 " saves one resize "
                                                 "per image, but pixels differ slightly; use --approx to apply it");
                }
            }
            break;
        }
    }
    removeMarked(program, remove);
}
