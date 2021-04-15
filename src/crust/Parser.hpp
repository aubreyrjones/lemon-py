#pragma once
#include <memory>
#include <vector>
#include <variant>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>

/** Used to intern strings found by the lexer. */
class StringTable {
protected:
	std::vector<std::string> strings;

	typedef std::unordered_map<std::string, size_t> PrevMap;

	PrevMap previousLocations;

public:
	size_t pushString(std::string const& s);  // add a string to the table

	std::string const& getString(size_t index); // get a string
};


/**
 * It seems Token must be a trivial value type to pass through
 * the lemon parser. This means we need to play tricks with
 * the value string.
 * 
*/
struct Token {
    int type = -1;
    size_t valueIndex;
    StringTable *valueTable;

    std::string const& value() const { valueTable->getString(valueIndex); }
};


/** Either a production name or a token value. */
using ParseValue = std::variant<std::string, Token>;


/**
 * ParseNodes are handled by pointer within the parser.
*/
struct ParseNode {
    ParseValue value;
    int64_t line;
    std::vector<ParseNode*> children;

    void push_back(ParseNode *n) { children.push_back(n); }
};


/**
 * Parser state, passed to the grammar actions.
*/
struct Parser {
    using ChildrenPack = std::initializer_list<ParseNode*>;

    std::vector<std::unique_ptr<ParseNode>> allNodes;

    ParseNode* make_node(ParseValue const& value, ChildrenPack const& children, int64_t line = -1);

    void error();
	void success();
};