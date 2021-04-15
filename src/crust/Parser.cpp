#include "Parser.hpp"
#include <iostream>

size_t StringTable::pushString(std::string const &s) {
	PrevMap::iterator it = previousLocations.find(s);
	if (it != previousLocations.end()){
		return (*it).second;
	}

	size_t idx = strings.size();
	previousLocations.emplace(s, idx);

	strings.push_back(s);

	return idx;
}

std::string const &StringTable::getString(size_t index) {
	return strings[index];
}


 ParseNode* Parser::make_node(ParseValue const& value, ChildrenPack const& children, int64_t line) {
    auto node = std::make_unique<ParseNode>();
    node->value = value;
    node->line = line;
    node->children.insert(node->children.end(), children);

    auto retval = node.get();
    allNodes.push_back(std::move(node));

    return retval;
}


void Parser::error() {
    std::cout << "Error." << std::endl;
    exit(25);
}

void Parser::success() {
    std::cout << "Parse successful." << std::endl;
}