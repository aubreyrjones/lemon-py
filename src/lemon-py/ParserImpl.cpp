#line 2 "ParserImpl.cpp"
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
#include <regex>
#include <tuple>
#include "concat_grammar.h"

namespace _parser_impl {
    struct Token;
    struct Parser;
}

void* PRSLParseAlloc(void *(*mallocProc)(size_t));
void PRSLParseFree(void *p, void (*freeProc)(void*));
void PRSLParse(void *yyp, int yymajor, _parser_impl::Token yyminor, _parser_impl::Parser *);


namespace py = pybind11;

namespace _parser_impl {

/** Used to intern strings found by the lexer. */
class StringTable {
protected:
	std::vector<std::string> strings;

	typedef std::unordered_map<std::string, size_t> PrevMap;

	PrevMap previousLocations;

public:
	size_t pushString(std::string const& s) {
        PrevMap::iterator it = previousLocations.find(s);
        if (it != previousLocations.end()){
            return (*it).second;
        }

        size_t idx = strings.size();
        previousLocations.emplace(s, idx);

        strings.push_back(s);

        return idx;
    }
        

	std::string const& getString(size_t index)  {
    	return strings[index];
    }
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

Token make_token(int type) {
    return Token {type, 0, nullptr};
}

Token make_token(int type, StringTable & st, std::string const& s) {
    return Token { type, st.pushString(s), &st };
}

template <typename V_T>
struct PTNode {
    char code;
    std::optional<V_T> value;

    std::vector<PTNode> children;

    PTNode(char code, std::optional<V_T> const& value) : code(code), value(value), children() {}

    void add_value(std::string_view const& code, V_T const& value) {
        if (code.length() == 0) {
            this->value = value;
            return;
        }

        for (auto & c : children) {
            if (c.code == code[0]) {
                c.add_value(code.substr(1, code.length() - 1), value);
                return;
            }
        }

        children.emplace_back(code[0], std::nullopt);
        children.back().add_value(code.substr(1, code.length() - 1), value);
    }

    using LexResult = std::tuple<V_T, std::string::const_iterator>;

    std::optional<LexResult> tryValue(std::string::const_iterator first, std::string::const_iterator last) const {
        if (first + 1 != last) {
            for (auto const& c : children) {
                if (*first == c.code) {
                    return c.tryValue(first + 1, last);
                }
            }
        }

        if (value && *first == code) {
            return std::make_tuple(value.value(), first + 1);
        }

        return std::nullopt;
    }
};

struct Lexer {
    static PTNode<int> literals;
    static void add_literal(int tok_value, std::string const& code) {
        literals.add_value(code, tok_value);
    }

    static std::vector<std::regex> skips;
    static void add_skip(std::string const& r) {
        skips.push_back(std::regex(r, std::regex::icase | std::regex::extended));
    }

    static std::vector<std::tuple<std::regex, int>> valueTypes;
    static void add_value_type(int tok_value, std::string const& r) {
        valueTypes.push_back(std::make_tuple(std::regex(r, std::regex::icase | std::regex::extended), tok_value));
    }

    // == instance ==

    std::string input;
    std::string::const_iterator curPos;
    StringTable &stringTable;

    Lexer(std::string const& inputString, StringTable & stringTable) : input(inputString), curPos(input.cbegin()), stringTable(stringTable) {}

    void advanceBy(size_t count) {
        while(count--) { ++curPos; }
    }

    void advanceBy(std::string_view const& sv) {
        auto count = sv.length();
        while (count && curPos != input.end()) {
            ++curPos;
            count--;
        }
    }

    void skip() {
        bool skipped = false;
        do { 
            skipped = false;
            for (auto const& r : skips) {
                std::smatch results;
                if (std::regex_search(curPos, input.cend(), results, r)) {
                    skipped = true;
                    advanceBy(results.length());
                }
            }
        } while (skipped);
    }

    std::optional<Token> nextLiteral() {
        auto result = literals.tryValue(curPos, input.cend());
        if (!result) return std::nullopt;

        curPos = std::get<1>(result.value()); // don't need to advance
        return /*make token*/ std::nullopt;
    }

    std::optional<Token> nextValue() {
        for (auto const& r : valueTypes) {
            std::smatch results;
            if (std::regex_search(curPos, input.cend(), results, std::get<0>(r))) {
                auto match_iterator = results.begin();
                if (results.size() > 1) { // skip past the whole match to get a submatch
                    std::advance(match_iterator, 1);
                }

                std::string value = (*match_iterator).str();;

                // for (; match_iterator != results.end(); std::advance(match_iterator, 1)) {
                //     value = 
                // }

                advanceBy(results.length());
                return make_token(std::get<1>(r), stringTable, value);
            }
        }
        return std::nullopt;
    }

    std::optional<Token> next() {
        skip();
        
        if (auto lit = nextLiteral()) {
            return lit;
        }
        else if (auto value = nextValue()) {
            return value;
        }

        return std::nullopt;
    };
};

// root of literals tree
PTNode<int> Lexer::literals(0, std::nullopt);

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

    ParseNode* make_node(ParseValue const& value, ChildrenPack const& children, int64_t line = -1) {
        auto node = std::make_unique<ParseNode>();
        node->value = value;
        node->line = line;
        node->children.insert(node->children.end(), children);

        auto retval = node.get();
        allNodes.push_back(std::move(node));

        return retval;
    }

    void error() {
        std::cout << "Error." << std::endl;
        exit(25);
    }

	void success() {
        std::cout << "Parse successful." << std::endl;
    }
};


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
    .def_readonly("line", &parser::ParseNode::line, "Line number of appearance.")
    .def_readonly("c", &parser::ParseNode::children, "Children.");
}



