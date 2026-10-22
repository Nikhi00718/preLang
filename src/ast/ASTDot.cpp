#include "ast/ASTDot.h"

#include "ast/ASTPrinter.h"

namespace {

std::string escape(const std::string& text) {
    std::string escaped;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

}  // namespace

ASTDot::ASTDot(std::ostream& out) : out_(out) {}

void ASTDot::write(const Program& program) {
    out_ << "digraph AST {\n  node [fontname=\"Consolas\", fontsize=11];\n";
    parent_ = addNode("Program", false);
    const int root = parent_;
    for (const auto& statement : program.statements) {
        parent_ = root;
        statement->accept(*this);
    }
    out_ << "}\n";
}

// Adds a node under parent_ (except the root) and returns its id.
// Syntax nodes are boxes; the values stored in them are plain text.
int ASTDot::addNode(const std::string& label, bool isField) {
    const int id = nextId_++;
    out_ << "  n" << id << " [label=\"" << escape(label) << "\""
         << (isField ? ", shape=plaintext" : ", shape=box, style=\"rounded,filled\", fillcolor=\"#EEF2FF\"") << "];\n";
    if (id != 0) {
        out_ << "  n" << parent_ << " -> n" << id << ";\n";
    }
    return id;
}

void ASTDot::addStatement(const std::string& label, const std::vector<std::string>& fields) {
    parent_ = addNode(label, false);
    for (const std::string& field : fields) {
        addNode(field, true);
    }
}

void ASTDot::visit(const LoadStmt& node) {
    addStatement("Load " + dataKindName(node.kind), {"\"" + node.path + "\"", node.dataset});
}

void ASTDot::visit(const DropNullStmt& node) {
    addStatement("DropNull", {selectorText(node.target)});
}

void ASTDot::visit(const FillNullStmt& node) {
    addStatement("FillNull", {selectorText(node.target), node.method == FillMethod::Value
                                                             ? literalText(node.value)
                                                             : std::string(fillMethodName(node.method))});
}

void ASTDot::visit(const NormalizeStmt& node) {
    addStatement("Normalize", {selectorText(node.target)});
}

void ASTDot::visit(const FilterStmt& node) {
    addStatement("Filter", {selectorText(node.target)});
    node.condition->accept(*this);
}

void ASTDot::visit(const SelectStmt& node) {
    std::vector<std::string> fields = {selectorText(node.target)};
    fields.insert(fields.end(), node.columns.begin(), node.columns.end());
    addStatement("Select", fields);
}

void ASTDot::visit(const DropStmt& node) {
    std::vector<std::string> fields = {selectorText(node.target)};
    fields.insert(fields.end(), node.columns.begin(), node.columns.end());
    addStatement("Drop", fields);
}

void ASTDot::visit(const ParseDateStmt& node) {
    addStatement("ParseDate", {selectorText(node.target)});
}

void ASTDot::visit(const DeriveStmt& node) {
    addStatement("Derive", {node.target.text(), valueText(*node.value)});
}

void ASTDot::visit(const JoinStmt& node) {
    addStatement("Join", {node.target.text(), "WITH " + node.other.text(), "ON " + node.key});
}

void ASTDot::visit(const AggregateStmt& node) {
    std::vector<std::string> fields = {node.target.text()};
    for (const std::string& column : node.by) {
        fields.push_back("BY " + column);
    }
    for (const AggregateItem& item : node.items) {
        fields.push_back(item.function + (item.column.empty() ? "" : " " + item.column) + " AS " + item.name);
    }
    addStatement("Aggregate", fields);
}

void ASTDot::visit(const SaveStmt& node) {
    addStatement("Save " + dataKindName(node.kind), {node.target.text(), "\"" + node.path + "\""});
}

void ASTDot::visit(const ExpectStmt& node) {
    addStatement("Expect", {selectorText(node.target), constraintText(node)});
}

void ASTDot::visit(const ResizeStmt& node) {
    addStatement("Resize", {node.dataset, std::to_string(node.width) + " x " + std::to_string(node.height) +
                                              (node.keepAspect ? " KEEP_ASPECT" : "")});
}

void ASTDot::visit(const GrayscaleStmt& node) {
    addStatement("Grayscale", {node.dataset});
}

void ASTDot::visit(const CompressStmt& node) {
    addStatement("Compress", {node.dataset, "QUALITY " + std::to_string(node.quality)});
}

void ASTDot::visit(const ConvertStmt& node) {
    addStatement("Convert", {node.dataset, node.format});
}

void ASTDot::visit(const RemoveCorruptStmt& node) {
    addStatement("RemoveCorrupt", {node.dataset});
}

void ASTDot::visit(const LinkStmt& node) {
    addStatement("Link", {node.images, node.table, "ON " + node.column, "AS " + node.dataset});
}

void ASTDot::visit(const ComparisonExpr& node) {
    addNode(node.column + " " + node.op + " " + literalText(node.value), false);
}

void ASTDot::visit(const LogicalExpr& node) {
    const int self = addNode(node.op == LogicalOp::And ? "AND" : "OR", false);
    parent_ = self;
    node.left->accept(*this);
    parent_ = self;
    node.right->accept(*this);
}

void ASTDot::visit(const NotExpr& node) {
    parent_ = addNode("NOT", false);
    node.operand->accept(*this);
}
