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
#include <sstream>
#include "concat_grammar.h"
#include <cstdio>

namespace _parser_impl {
    struct Token;
    struct Parser;
}

void* LemonPyParseAlloc(void *(*mallocProc)(size_t));
void LemonPyParseFree(void *p, void (*freeProc)(void*));
void LemonPyParse(void *yyp, int yymajor, _parser_impl::Token yyminor, _parser_impl::Parser *);


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

std::unordered_map<int, std::string> token_name_map;

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
    int line;

    std::string value() const { 
        if (valueTable) return valueTable->getString(valueIndex); 
        return "LITERAL";
    }

    std::string const& name() const {
        return token_name_map[type];
    }

    std::string toString() const {
        auto const& tokenName = token_name_map[type];

        char outbuf[1024]; // just do the first 1k characters
        if (valueTable) {
            snprintf(outbuf, 1024, "%s <%s> (line %d)", tokenName.c_str(), value().c_str(), line);
        }
        else {
            snprintf(outbuf, 1024, "%s (line %d)", tokenName.c_str(), line);
        }

        return std::string(outbuf);
    }
};

Token make_token(int type, int line) {
    return Token {type, 0, nullptr, line};
}

Token make_token(int type, StringTable & st, std::string const& s, int line) {
    return Token {type, st.pushString(s), &st, line};
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
        //std::cout << "Checking " << std::string(first, last) << " against " << code << std::endl;
        if (children.empty() || first == last) { // reached end of input or end of chain while still matching.
            goto bailout;
        }

        for (auto const& c : children) {
            if (*first == c.code) {
                if (auto found = c.tryValue(first + 1, last)) {
                    return found;
                }
            }
        }

        bailout:
        if (value) { // 
                return std::make_tuple(value.value(), first);
        }

        return std::nullopt;
    }
};

/**
 * This is a relatively basic lexer. It handles two classes of tokens, plus skip patterns.
 * 
 * "literal" tokens are defined by a fixed string of characters, and are stored in a basic
 * prefix tree (PTNode above). These are matched greedily, with the longest matching sequence
 * having priority. Literal tokens are returned by lemon-defined code number, without a value.
 * 
 * "value" tokens are defined by a regular expression, and are returned with both a code
 * and a value. A single sub-match may be used to denote a partial value extraction from
 * the overall token match. No type conversions are done, all values are strings.
 * 
 * Value token patterns are checked in the same order they are defined with `add_value_type`.
 * 
 * Skip patterns are simply regexes that are used to skip whitespace, comments, or other
 * lexically and syntactically-irrelevant content. Skip patterns are applied before every
 * attempt at token extraction.
 * 
*/
struct Lexer {
    static PTNode<int> literals;
    static void add_literal(int tok_code, std::string const& code) {
        literals.add_value(code, tok_code);
    }

    static std::vector<std::regex> skips;
    static void add_skip(std::string const& r) {
        skips.push_back(std::regex(r, std::regex::icase | std::regex::ECMAScript));
    }

    static std::vector<std::tuple<std::regex, int>> valueTypes;
    static void add_value_type(int tok_code, std::string const& r) {
        valueTypes.push_back(std::make_tuple(std::regex(r, std::regex::icase | std::regex::ECMAScript), tok_code));
    }

    static int singleQuoteToken;
    static int doubleQuoteToken;

    using siter = std::string::const_iterator;
    // == instance ==
private:
    std::string input;
    std::string::const_iterator curPos;
    StringTable &stringTable;
    int count;
    bool reachedEnd;
    int line = 1;
    

    siter advanceBy(size_t count) {
        auto oldPos = curPos;
        std::advance(curPos, count);
        line += countLines(oldPos, curPos);
        return oldPos;
    }

    siter advanceTo(siter const& newPos) {
        auto oldPos = curPos;
        curPos = newPos;
        line += countLines(oldPos, curPos);

        return oldPos;
    }

    int countLines(siter from, siter const& to) {
        int lineCount = 0;
        for (; from != to; from++) {
            if (*from == '\n') {
                lineCount++;
            }
        }
        return lineCount;
    }

    void skip() {
        bool skipped = false;
        do { 
            skipped = false;
            for (auto const& r : skips) {
                std::smatch results;
                if (std::regex_search(curPos, input.cend(), results, r, std::regex_constants::match_continuous)) {
                    skipped = true;
                    advanceBy(results.length());
                }
            }
        } while (skipped);
    }

    siter stringEnd(char stringDelim, siter stringStart, siter end) {
        for (; stringStart != end; ++stringStart) {
            if (*stringStart == '\\') {
                if (*(stringStart + 1) == stringDelim || *(stringStart + 1) == '\\') {
                    stringStart++; // skip past this delim
                }
            }
            else if (*stringStart == stringDelim) {
                return stringStart;
            }
        }

        throw std::runtime_error("Lexer error: string lexing reached end of input.");
    }

    std::optional<Token> nextString() {
        auto n = [this] (int tokCode, char delim) -> std::optional<Token> {
            if (tokCode && *curPos == delim) {
                auto send = stringEnd(delim, curPos + 1, input.cend());
                auto sstart = advanceTo(send + 1);
                return make_token(tokCode, stringTable, std::string(sstart + 1, send), line);
            }
            return std::nullopt;
        };

        if (auto s = n(singleQuoteToken, '\'')) {
            return s;
        }
        
        if (auto s = n(doubleQuoteToken, '"')) {
            return s;
        }

        return std::nullopt;
    }

    std::optional<Token> nextLiteral() {
        auto result = literals.tryValue(curPos, input.cend());
        if (!result) return std::nullopt;

        advanceTo(std::get<1>(result.value()));
        return make_token(std::get<0>(result.value()), line);
    }

    std::optional<Token> nextValue() {
        for (auto const& r : valueTypes) {
            std::smatch results;
            if (std::regex_search(curPos, input.cend(), results, std::get<0>(r), std::regex_constants::match_continuous)) {
                auto match_iterator = results.begin();
                if (results.size() > 1) { // skip past the whole match to get a submatch
                    std::advance(match_iterator, 1);
                }

                std::string value = (*match_iterator).str();

                advanceBy(results.length()); // advance by length of _entire_ match
                return make_token(std::get<1>(r), stringTable, value, line);
            }
        }
        return std::nullopt;
    }

public:
    Lexer(std::string const& inputString, StringTable & stringTable) : input(inputString), curPos(input.cbegin()), stringTable(stringTable), count(0), reachedEnd(false) {}

    std::optional<Token> next() {
        skip();

        if (consumedInput()) {
            if (reachedEnd) {
                return std::nullopt;
            }
            else {
                reachedEnd = true;
                return make_token(0, line);
            }
        }
        
        if (auto str = nextString()) {
            count++;
            return str;
        }
        else if (auto lit = nextLiteral()) {
            count++;
            return lit;
        }
        else if (auto value = nextValue()) {
            count++;
            return value;
        }

        return std::nullopt;
    };

    int const& getLine() const { return line; }

    bool consumedInput() {
        return curPos == input.cend();
    }

    std::string remainder(size_t len = 0) {
        return std::string(curPos, len && (curPos + len < input.cend()) ? (curPos + len) : input.cend());
    }

    int getCount() const {
        return count;
    }
};

void _init_lexer();

int Lexer::singleQuoteToken = 0;
int Lexer::doubleQuoteToken = 0;
PTNode<int> Lexer::literals(0, std::nullopt);
std::vector<std::regex> Lexer::skips;
std::vector<std::tuple<std::regex, int>> Lexer::valueTypes;


/** Either a production name or a token value. */
using ParseValue = std::variant<std::string, Token>;

/**
 * ParseNodes are handled by pointer within the parser.
*/
struct ParseNode {
    ParseValue value;
    int64_t line;
    std::vector<ParseNode*> children;

    ParseNode* push_back(ParseNode *n) { children.push_back(n); return this; }

    ParseNode* pb(ParseNode *n) { return push_back(n); }

    ParseNode* l(int64_t line) { this->line = line; return this; }
};

/**
 * Parser state, passed to the grammar actions.
*/
struct Parser {
    void* lemonParser;
    std::unordered_map<ParseNode*, std::unique_ptr<ParseNode>> allNodes;
    StringTable stringTable;
    Token currentToken;

    ParseNode *root = nullptr;

    Parser() : lemonParser(LemonPyParseAlloc(malloc)), allNodes(), stringTable() {
        _init_lexer();
    }

    ~Parser() {
        LemonPyParseFree(lemonParser, free);
    }

    using ChildrenPack = std::initializer_list<ParseNode*>;
    ParseNode* make_node(ParseValue const& value, ChildrenPack const& children = {}, int64_t line = -1) {
        auto node = std::make_unique<ParseNode>();
        node->value = value;
        if (std::holds_alternative<Token>(value)) {
            node-> line = std::get<Token>(value).line;
        }
        else {
            node->line = line;
        }

        node->children.insert(node->children.end(), children);

        auto retval = node.get();
        allNodes.emplace(retval, std::move(node));

        return retval;
    }

    ParseNode* mn(ParseValue const& value, ChildrenPack const& children = {}, int64_t line = -1) {
        return make_node(value, children, line);
    }

    void drop_node(ParseNode *pn) {
        auto it = allNodes.find(pn);
        if (it != allNodes.end()) {
            allNodes.erase(it);
        }
    }

    void offerToken(Token token) {
        currentToken = token;
	    LemonPyParse(lemonParser, token.type, token, this);
    }

    void error() {
        throw std::runtime_error("Parse error on token: " + currentToken.toString());
    }

	void success() {
        //nop
    }

    ParseNode* push_root(ParseNode *pn) {
        return root = pn;
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

std::string sanitize(std::string in) {
    auto clean = [&in] (char c, const char* replace) {
        size_t res = 0;
        while((res = in.find(c, res)) != std::string::npos) {
            in.erase(res, 1);
            in.insert(res, replace);
        }
    };

    clean('&', "&amp;");
    clean('"', "&quot;");
    clean('\'', "&apos;");
    clean('<', "&lt;");
    clean('>', "&gt;");

    return std::move(in);
}

struct ParseNode {
    std::optional<std::string> production;
    std::optional<std::string> tokName;
    std::optional<std::string> value;
    int64_t line;
    std::vector<ParseNode> children;
    int id;

    ParseNode() : production(), tokName(), value(), line(-1), children(), id(-1) {}
    ParseNode(ParseNode && o) : production(std::move(o.production)), tokName(std::move(o.tokName)), value(std::move(o.value)), line(o.line), children(std::move(o.children)), id(o.id) {
        o.id = -1;
    }

    ParseNode& operator=(ParseNode && o) {
        using namespace std;
        production = move(o.production);
        tokName = move(o.tokName);
        value = move(o.value);
        line = o.line;
        children = move(o.children);
        id = o.id;
        o.id = -1;

        return *this;
    }


    ParseNode(ParseNode const&) = default;
    ParseNode& operator=(ParseNode const&) = default;

    py::object getProduction() const {
        return string_or_none(production);
    }

    py::object getValue() const {
        return string_or_none(value);
    }

    py::object getToken() const {
        return string_or_none(tokName);
    }

    std::string toString() {
        char outbuf[1024]; // just do the first 1k characters
        if (production) {
            snprintf(outbuf, 1024, "{%s} [%lu]", production.value().c_str(), children.size());
        }
        else {
            snprintf(outbuf, 1024, "%s <%s>", tokName.value().c_str(), value.value().c_str());
        }

        return std::string(outbuf);
    }

    void dotify(std::stringstream & out, const ParseNode * parent) const {
        char buf[1024];

        if (production) {
            snprintf(buf, 1024, "node [shape=record, label=\"{<f0>line:%ld | <f1> %s }\"] %d;\n", line, sanitize(production.value()).c_str(), id);
        }
        else {
            snprintf(buf, 1024, "node [shape=record, label=\"{<f0>line:%ld | { <f1> %s | <f2> %s}}\"] %d;\n", line, sanitize(tokName.value()).c_str(), sanitize(value.value()).c_str(), id);
        }
        out << buf;

        if (parent) {
            snprintf(buf, 1024, "%d -> %d;\n", parent->id, id);
            out << buf;
        }

        for (auto const& c : children) {
            c.dotify(out, this);
        }
    }
};

std::string dotify(ParseNode const& pn) {
    std::stringstream out;

    out << "digraph \"AST\" { \n";
	out << "node [shape=record, style=filled];\n\n";

    pn.dotify(out, nullptr);

    out << "\n}\n";

    return out.str();
}

ParseNode uplift_node(_parser_impl::ParseNode* alien, int & idCounter) {
    ParseNode retval;
    retval.id = idCounter++;

    if (std::holds_alternative<_parser_impl::Token>(alien->value)) {
        auto tok = std::get<_parser_impl::Token>(alien->value);
        retval.tokName = tok.name();
        retval.value = tok.value();
        //std::cout << "uplifting " << retval.value.value() << std::endl;
    }
    else {
        retval.production = std::get<std::string>(alien->value);
        //std::cout << "uplifting " << retval.production.value() << std::endl;
    }
    retval.line = alien->line;
    
    for (auto c : alien->children) {
        retval.children.push_back(uplift_node(c, idCounter));
    }

    return std::move(retval);
}

ParseNode uplift_node(_parser_impl::ParseNode* alien) {
    int idCounter = 0;
    return uplift_node(alien, idCounter);
}

ParseNode parse_string(std::string const& input) {
    using namespace _parser_impl;
    Parser p;
    Lexer lexer(input, p.stringTable);

    while (auto tok = lexer.next()) {
         p.offerToken(tok.value());
    }

    if (!lexer.consumedInput()) {
        char buf[1024];
        snprintf(buf, 1024, "Lexer failure on line %d. Around here:\n", lexer.getLine());
        throw std::runtime_error(std::string(buf) + lexer.remainder(100));
    }

    if (p.root) { // successful parse.
        return uplift_node(p.root);
    }

    return ParseNode();
}

}


PYBIND11_MODULE(PYTHON_PARSER_MODULE_NAME, m) {
    m.def("parse", &parser::parse_string, "Parse a string into a parse tree.");
    m.def("dotify", &parser::dotify, "Get a graphviz DOT representation of the parse tree.");

    py::class_<parser::ParseNode>(m, "Node")
    .def(py::init<>())
    .def("__repr__", &parser::ParseNode::toString, "Get an approximation of the representation.")
    .def_property_readonly("production", &parser::ParseNode::getProduction, "Get production if non-terminal.")
    .def_property_readonly("type", &parser::ParseNode::getToken, "Get type if terminal.")
    .def_property_readonly("value", &parser::ParseNode::getValue, "Get value if terminal.")
    .def_readonly("line", &parser::ParseNode::line, "Line number of appearance.")
    .def_readonly("c", &parser::ParseNode::children, "Children.");
}

#define pnn(v) p->make_node(v)

