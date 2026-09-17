#include "ast/ASTPrinter.h"

namespace {

std::string columnText(const ColumnRef& ref) {
    return ref.dataset + "." + ref.column;
}

std::string literalText(const Literal& literal) {
    return literal.kind == LiteralKind::String ? "\"" + literal.value + "\"" : literal.value;
}

std::string listText(const std::vector<std::string>& names) {
    std::string text = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        text += (i > 0 ? ", " : "") + names[i];
    }
    return text + "]";
}

std::string constraintText(const ExpectStmt& node) {
    switch (node.constraint) {
        case ConstraintKind::IsNumber:  return "NUMBER";
        case ConstraintKind::IsString:  return "STRING";
        case ConstraintKind::IsBoolean: return "BOOLEAN";
        case ConstraintKind::NotNull:   return "NOT_NULL";
        case ConstraintKind::Between:   return "BETWEEN " + node.low + " AND " + node.high;
    }
    return "";
}

}  // namespace

ASTPrinter::ASTPrinter(std::ostream& out) : out_(out) {}

void ASTPrinter::print(const Program& program) {
    out_ << "Program\n";
    for (size_t i = 0; i < program.statements.size(); ++i) {
        isLastChild_ = (i + 1 == program.statements.size());
        program.statements[i]->accept(*this);
    }
}

// ---------- Statements ----------

void ASTPrinter::visit(const LoadStmt& node) {
    printNode("Load", {"file: \"" + node.path + "\"", "dataset: " + node.dataset});
}

void ASTPrinter::visit(const DropNullStmt& node) {
    printNode("DropNull", {"dataset: " + node.dataset});
}

void ASTPrinter::visit(const FillNullStmt& node) {
    printNode("FillNull", {"column: " + columnText(node.target), "value: " + literalText(node.value)});
}

void ASTPrinter::visit(const NormalizeStmt& node) {
    const std::string method = node.method == NormalizeMethod::ZScore ? "ZSCORE" : "MINMAX";
    printNode("Normalize", {"column: " + columnText(node.target), "method: " + method});
}

void ASTPrinter::visit(const FilterStmt& node) {
    const bool isLast = isLastChild_;
    printLine("Filter", isLast);
    printChildren(isLast, [&] {
        printLine("dataset: " + node.dataset, false);
        printLine("condition", true);
        printChildren(true, [&] {
            isLastChild_ = true;
            node.condition->accept(*this);
        });
    });
}

void ASTPrinter::visit(const SelectStmt& node) {
    printNode("Select", {"dataset: " + node.dataset, "columns: " + listText(node.columns)});
}

void ASTPrinter::visit(const DropStmt& node) {
    printNode("Drop", {"dataset: " + node.dataset, "columns: " + listText(node.columns)});
}

void ASTPrinter::visit(const SaveStmt& node) {
    printNode("Save", {"dataset: " + node.dataset, "file: \"" + node.path + "\""});
}

void ASTPrinter::visit(const ExpectStmt& node) {
    printNode("Expect", {"column: " + columnText(node.target), "constraint: " + constraintText(node)});
}

// ---------- Conditions ----------

void ASTPrinter::visit(const ComparisonExpr& node) {
    printLine("Compare: " + node.column + " " + node.op + " " + literalText(node.value), isLastChild_);
}

void ASTPrinter::visit(const LogicalExpr& node) {
    const bool isLast = isLastChild_;
    printLine(node.op == LogicalOp::And ? "AND" : "OR", isLast);
    printChildren(isLast, [&] {
        isLastChild_ = false;
        node.left->accept(*this);
        isLastChild_ = true;
        node.right->accept(*this);
    });
}

void ASTPrinter::visit(const NotExpr& node) {
    const bool isLast = isLastChild_;
    printLine("NOT", isLast);
    printChildren(isLast, [&] {
        isLastChild_ = true;
        node.operand->accept(*this);
    });
}

// ---------- Tree drawing ----------

void ASTPrinter::printLine(const std::string& text, bool isLast) {
    out_ << indent_ << (isLast ? "`-- " : "|-- ") << text << '\n';
}

// Everything printed inside printBody is drawn one level deeper. A vertical
// bar continues down only if the parent still has siblings below it.
void ASTPrinter::printChildren(bool parentIsLast, const std::function<void()>& printBody) {
    const std::string saved = indent_;
    indent_ += parentIsLast ? "    " : "|   ";
    printBody();
    indent_ = saved;
}

// A node whose children are all simple "name: value" lines.
void ASTPrinter::printNode(const std::string& label, const std::vector<std::string>& fields) {
    const bool isLast = isLastChild_;
    printLine(label, isLast);
    printChildren(isLast, [&] {
        for (size_t i = 0; i < fields.size(); ++i) {
            printLine(fields[i], i + 1 == fields.size());
        }
    });
}
