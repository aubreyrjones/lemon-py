#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <vector>
#include <variant>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include <iostream>

namespace py = pybind11;

namespace _parser_impl {
    struct Token;
    struct Parser;
}

void* PRSLParseAlloc(void *(*mallocProc)(size_t));
void PRSLParseFree(void *p, void (*freeProc)(void*));
void PRSLParse(void *yyp, int yymajor, _parser_impl::Token yyminor, _parser_impl::Parser *);

namespace _parser_impl {

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
    int type;
    size_t valueIndex;
    StringTable *valueTable;

    std::string const& value() const { return valueTable->getString(valueIndex); }
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

} // namespace


namespace parser {

py::object string_or_none(std::optional<std::string> const& v) {
    if (!v) {
        //return py::cast<py::none>(Py_None);
        return py::none{};
    }
    else {
        return py::cast<py::str>(PyUnicode_FromStringAndSize(v.value().data(), v.value().length()));
    }
}

struct ParseNode {
    std::optional<std::string> production;
    std::optional<std::string> value;
    int64_t line;
    std::vector<ParseNode> children;

    py::object getProduction() const {
        return string_or_none(production);
    }

    py::object getValue() const {
        return string_or_none(value);
    }
};

ParseNode parse_string(std::string const& source_code) {
    return ParseNode();
}

}


PYBIND11_MODULE(PYTHON_PARSER_MODULE_NAME, m) {
    m.def("parse", &parser::parse_string, "Parse a string into a parse tree.");

    py::class_<parser::ParseNode>(m, "Node")
    .def(py::init<>())
    .def("production", &parser::ParseNode::getProduction, "Get production if non-terminal.")
    .def("value", &parser::ParseNode::getValue, "Get value if terminal.")
    .def_readonly("line", &parser::ParseNode::line, "Line number of definition.")
    .def_readonly("c", &parser::ParseNode::children, "Children.");
}