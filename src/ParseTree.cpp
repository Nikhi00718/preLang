#include "ParseTree.h"

namespace prep {

void printParseTree(const ParseTreeNode& node, std::ostream& out,
                    const std::string& indent, bool last) {
    out << indent << (last ? "\\-- " : "|-- ") << node.name;
    if (!node.value.empty()) out << ": " << node.value;
    out << '\n';

    std::string childIndent = indent + (last ? "    " : "|   ");
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        printParseTree(node.children[i], out, childIndent,
                       i + 1 == node.children.size());
    }
}

}  // namespace prep
