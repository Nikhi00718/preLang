#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace prep {

struct ParseTreeNode {
    std::string name;
    std::string value;
    std::vector<ParseTreeNode> children;
};

void printParseTree(const ParseTreeNode& node, std::ostream& out,
                    const std::string& indent = "", bool last = true);

}  // namespace prep
