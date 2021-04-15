#pragma once
#include <optional>
#include <vector>
#include <string>
#include <cstdint>

namespace crust {

struct ParseNode {
    std::optional<std::string> production;
    std::optional<std::string> value;
    int64_t line;
    std::vector<ParseNode> children;
};

}