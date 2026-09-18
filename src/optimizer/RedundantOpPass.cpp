#include "optimizer/RedundantOpPass.h"

RedundantOpPass::RedundantOpPass(bool approximate) : approximate_(approximate) {}

void RedundantOpPass::run(IRProgram& program, OptimizationReport& report) {
    std::vector<bool> remove(program.code.size(), false);
    for (size_t i = 0; i < program.code.size(); ++i) {
        const Instruction& instruction = program.code[i];
        if (instruction.noEffect.empty()) {
            continue;
        }
        remove[i] = true;
        const std::string kind =
            instruction.op == Opcode::Check ? "contract check elimination" : "no-effect elimination";
        report.changes.push_back(kind + ": removed " + describe(instruction) + ", " + instruction.noEffect);
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
        if (first.op != Opcode::Resize || first.numbers[2] == 1) {
            continue;
        }
        for (size_t j = i + 1; j < program.code.size(); ++j) {
            const Instruction& next = program.code[j];
            if (next.dataset != first.dataset || next.op == Opcode::Compress || next.op == Opcode::Convert) {
                continue;
            }
            if (next.op == Opcode::Resize && next.numbers[2] == 0) {
                if (approximate_) {
                    remove[i] = true;
                    report.changes.push_back("RESIZE fusion (approximate): removed " + describe(first) + ", " +
                                             describe(next) + " alone gives almost the same pixels");
                } else {
                    report.suggestions.push_back("RESIZE fusion: removing " + describe(first) + " saves one resize "
                                                 "per image, but pixels differ slightly; use --approx to apply it");
                }
            }
            break;
        }
    }
    removeMarked(program, remove);
}
