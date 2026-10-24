#include "ast/ASTPrinter.h"

#include <algorithm>

namespace {

std::string listText(const std::vector<std::string>& names) {
    std::string text = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        text += (i > 0 ? ", " : "") + names[i];
    }
    return text + "]";
}

std::string methodText(const NormalizeStmt& node) {
    switch (node.method) {
        case NormalizeMethod::ZScore: return "ZSCORE";
        case NormalizeMethod::MinMax: return "MINMAX";
        case NormalizeMethod::Range:  return "RANGE " + node.low + " " + node.high;
    }
    return "";
}

}  // namespace

std::string selectorText(const Selector& selector) {
    static const char* const filters[] = {"", " WHERE TYPE NUMERIC", " WHERE TYPE STRING", " WHERE TYPE BOOLEAN",
                                          " WHERE NULLABLE"};
    std::string text = selector.text() + filters[static_cast<int>(selector.filter)];
    if (!selector.except.empty()) {
        text += " EXCEPT " + listText(selector.except);
    }
    return text + (selector.ifExists ? " IF_EXISTS" : "");
}

std::string dataKindName(DataKind kind) {
    switch (kind) {
        case DataKind::Table:    return "TABLE";
        case DataKind::TableSet: return "TABLESET";
        case DataKind::Image:    return "IMAGE";
        case DataKind::ImageSet: return "IMAGESET";
    }
    return "";
}

std::string constraintText(const ExpectStmt& node) {
    switch (node.constraint) {
        case ConstraintKind::IsNumber:   return "NUMBER";
        case ConstraintKind::IsString:   return "STRING";
        case ConstraintKind::IsBoolean:  return "BOOLEAN";
        case ConstraintKind::NotNull:    return "NOT_NULL";
        case ConstraintKind::Between:    return "BETWEEN " + node.low + " AND " + node.high;
        case ConstraintKind::Unique:     return "UNIQUE";
        case ConstraintKind::References: return "REFERENCES " + node.other.text();
        case ConstraintKind::SchemaSameAs:
            return "SCHEMA_SAME_AS " + node.other.text() +
                   (node.except.empty() ? "" : " EXCEPT " + listText(node.except));
        case ConstraintKind::Channels:  return "CHANNELS " + node.low;
        case ConstraintKind::MinWidth:  return "MIN_WIDTH " + node.low;
        case ConstraintKind::MinHeight: return "MIN_HEIGHT " + node.low;
        case ConstraintKind::MatchRate: return "MATCH_RATE " + node.low + "%";
    }
    return "";
}

std::string literalText(const Literal& literal) {
    return literal.kind == LiteralKind::String ? "\"" + literal.value + "\"" : literal.value;
}

std::string valueText(const ValueExpr& expr) {
    switch (expr.kind) {
        case ValueKind::Number:
        case ValueKind::Column:   return expr.text;
        case ValueKind::DatePart: return expr.text + "(" + expr.left->text + ")";
        case ValueKind::Binary:   {
            // Parentheses only where the tree needs them: a - (b - c), a * (b + c), a / (b * c)
            const bool multiplies = expr.text == "*" || expr.text == "/";
            const auto operand = [&](const ValueExpr& side, bool right) {
                if (side.kind != ValueKind::Binary) {
                    return valueText(side);
                }
                const bool sideAdds = side.text == "+" || side.text == "-";
                const bool needs = (multiplies && sideAdds) ||
                                   (right && (expr.text == "-" || expr.text == "/") && multiplies == !sideAdds);
                return needs ? "(" + valueText(side) + ")" : valueText(side);
            };
            return operand(*expr.left, false) + " " + expr.text + " " + operand(*expr.right, true);
        }
    }
    return "";
}

std::vector<std::string> valueColumns(const ValueExpr& expr) {
    std::vector<std::string> columns;
    const std::function<void(const ValueExpr&)> collect = [&](const ValueExpr& node) {
        if (node.kind == ValueKind::Column && std::find(columns.begin(), columns.end(), node.text) == columns.end()) {
            columns.push_back(node.text);
        }
        if (node.left) {
            collect(*node.left);
        }
        if (node.right) {
            collect(*node.right);
        }
    };
    collect(expr);
    return columns;
}

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
    std::vector<std::string> fields = {"file: \"" + node.path + "\"", "dataset: " + node.dataset};
    if (node.recursive) {
        fields.push_back("recursive: yes");
    }
    printNode("Load " + dataKindName(node.kind), fields);
}

void ASTPrinter::visit(const DropNullStmt& node) {
    printNode("DropNull",
              {"target: " + selectorText(node.target) + (node.columns.empty() ? "" : " " + listText(node.columns))});
}

void ASTPrinter::visit(const FillNullStmt& node) {
    printNode("FillNull", {"target: " + selectorText(node.target),
                           "value: " + (node.method == FillMethod::Value ? literalText(node.value)
                                                                         : std::string(fillMethodName(node.method)))});
}

void ASTPrinter::visit(const NormalizeStmt& node) {
    printNode("Normalize", {"target: " + selectorText(node.target), "method: " + methodText(node)});
}

void ASTPrinter::visit(const FilterStmt& node) {
    const bool isLast = isLastChild_;
    printLine("Filter", isLast);
    printChildren(isLast, [&] {
        printLine("table: " + selectorText(node.target), false);
        printLine("condition", true);
        printChildren(true, [&] {
            isLastChild_ = true;
            node.condition->accept(*this);
        });
    });
}

void ASTPrinter::visit(const SelectStmt& node) {
    printNode("Select", {"table: " + selectorText(node.target), "columns: " + listText(node.columns)});
}

void ASTPrinter::visit(const DropStmt& node) {
    printNode("Drop", {"tables: " + selectorText(node.target), "columns: " + listText(node.columns)});
}

void ASTPrinter::visit(const ParseDateStmt& node) { printNode("ParseDate", {"column: " + selectorText(node.target)}); }

void ASTPrinter::visit(const DeriveStmt& node) {
    printNode("Derive", {"column: " + node.target.text(), "value: " + valueText(*node.value)});
}

void ASTPrinter::visit(const JoinStmt& node) {
    printNode("Join", {"table: " + node.target.text(), "with: " + node.other.text(), "on: " + node.key});
}

void ASTPrinter::visit(const AggregateStmt& node) {
    std::vector<std::string> fields = {"table: " + node.target.text(), "by: " + listText(node.by)};
    for (const AggregateItem& item : node.items) {
        fields.push_back(item.function + (item.column.empty() ? "" : " " + item.column) + " AS " + item.name);
    }
    printNode("Aggregate", fields);
}

void ASTPrinter::visit(const PlotStmt& node) {
    std::string columns;
    for (const std::string& column : node.columns) {
        columns += (columns.empty() ? "" : ", ") + column;
    }
    printNode("Plot", {"dataset: " + node.target.text(), "kind: " + node.kind, "columns: " + columns,
                       "bins: " + (node.bins > 0 ? std::to_string(node.bins) : std::string("default")),
                       "to: \"" + node.path + "\""});
}

void ASTPrinter::visit(const ReportStmt& node) {
    printNode("Report", {"dataset: " + node.target.text(), "to: \"" + node.path + "\""});
}

void ASTPrinter::visit(const SaveStmt& node) {
    printNode("Save " + dataKindName(node.kind), {"dataset: " + node.target.text(), "to: \"" + node.path + "\""});
}

void ASTPrinter::visit(const ExpectStmt& node) {
    printNode("Expect", {"target: " + selectorText(node.target), "contract: " + constraintText(node)});
}

void ASTPrinter::visit(const ResizeStmt& node) {
    printNode("Resize",
              {"images: " + node.dataset, "size: " + std::to_string(node.width) + " x " + std::to_string(node.height),
               std::string("keep aspect: ") + (node.keepAspect ? "yes" : "no")});
}

void ASTPrinter::visit(const GrayscaleStmt& node) { printNode("Grayscale", {"images: " + node.dataset}); }

void ASTPrinter::visit(const CompressStmt& node) {
    printNode("Compress", {"images: " + node.dataset, "quality: " + std::to_string(node.quality)});
}

void ASTPrinter::visit(const ConvertStmt& node) {
    printNode("Convert", {"images: " + node.dataset, "format: " + node.format});
}

void ASTPrinter::visit(const RemoveCorruptStmt& node) { printNode("RemoveCorrupt", {"images: " + node.dataset}); }

void ASTPrinter::visit(const LinkStmt& node) {
    printNode("Link", {"images: " + node.images, "table: " + node.table, "on: " + node.column, "as: " + node.dataset});
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
