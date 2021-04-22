/*
MIT License

Copyright (c) 2021 Aubrey R Jones

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#define LEMON_PY_UNICODE_SUPPORT

#include <ParseNode.hpp>

#include <memory>
#include <variant>
#include <cstdint>
#include <unordered_map>
#include <iostream>
#include <regex>
#include <tuple>
#include <cstdio>

// Forward declarations of types needed for Lemon function forward declarations
// it's turtles all the way down when you've got no headers lol
namespace _parser_impl {
    struct Token;
    struct Parser;
    struct GrammarActionParserHandle;
    struct GrammarActionNodeHandle;
}

// these are Lemon parser functions.

void* LemonPyParseAlloc(void *(*mallocProc)(size_t));
void LemonPyParseFree(void *p, void (*freeProc)(void*));
void LemonPyParse(void *, int, _parser_impl::Token, _parser_impl::GrammarActionParserHandle);
void LemonPyParseInit(void *);


#ifndef LEMON_PY_SUPPRESS_PYTHON

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>
namespace py = pybind11;

#endif

#ifdef LEMON_PY_UNICODE_SUPPORT
#include <utf.hpp>

namespace std {

template <>
class regex_traits<u32string::value_type> {
public:
    using shadow_type = regex_traits<wchar_t>;
    shadow_type shadow;

    regex_traits() : shadow() {}

    using char_type = u32string::value_type;
    using string_type = u32string;
    using locale_type = shadow_type::locale_type;
    using char_class_type = shadow_type::char_class_type;

    static size_t length(const char_type* p) { 
        return shadow_type::length((const wchar_t*) p);  //c-style casts because these should be the same type!
    }

    char_type translate(char_type c) const {
        return shadow.translate((wchar_t) c);
    }

    char_type translate_nocase(char_type c) const {
        return shadow.translate_nocase((wchar_t) c);
    }

    template <class IT>
    string_type transform(IT first, IT last) const {
        typedef std::collate<char_type> __collate_type;
	    const __collate_type& __fclt(use_facet<__collate_type>(shadow.getloc()));
	    string_type __s(first, last);
	    return __fclt.transform(__s.data(), __s.data() + __s.size());
    }

    template <class IT>
    string_type transform_primary(IT first, IT last) const {
	    typedef std::ctype<char_type> __ctype_type;
	    const __ctype_type& __fctyp(use_facet<__ctype_type>(shadow.getloc()));
	    std::vector<char_type> __s(first, last);
	    __fctyp.tolower(__s.data(), __s.data() + __s.size());
	    return this->transform(__s.data(), __s.data() + __s.size());
    }

    template< class ForwardIt >
    string_type lookup_collatename( ForwardIt first, ForwardIt last ) const {
        auto shadowVal = shadow.lookup_collatename(first, last);
        return string_type(shadowVal.data(), shadowVal.data() + shadowVal.size());
    }

    template< class ForwardIt >
    char_class_type lookup_classname( ForwardIt first, ForwardIt last, bool icase = false ) const {
        return shadow.lookup_classname(first, last, icase);
    }
	
	bool isctype( char_type c, char_class_type f ) const {
        return shadow.isctype(c, f);
    }

    int value( char_type ch, int radix ) const {
        return shadow.value(ch, radix);
    }

    locale_type imbue( locale_type loc ) { return shadow.imbue(loc); }

    locale_type getloc() const { return shadow.getloc(); }

};
}

#endif

namespace _parser_impl {

// ==================== UTILITIES AND DECLARATIONS =======================


#ifndef LEMON_PY_UNICODE_SUPPORT
using ustring = std::string;
using ustring_view = std::string_view;

std::string const& toExternal(_parser_impl::ustring const& ascii) {
    return ascii;
}

_parser_impl::ustring const& toInternal(std::string const& ascii) {
    return ascii;
}

#else

using ustring = std::u32string;
using ustring_view = std::u32string_view;

std::string toExternal(_parser_impl::ustring const& utf32) {
    return utf8::utf32to8(utf32);
}

_parser_impl::ustring toInternal(std::string const& utf8) {
    return utf8::utf8to32(utf8);
}
#endif

using sstream = std::basic_stringstream<ustring::value_type>;
using siter = ustring::const_iterator;
using uregex = std::basic_regex<ustring::value_type>;
using regex_results = std::match_results<siter>;
using uuchar = ustring::value_type;

//==================== TOKENS ==============================

/** Used to intern strings found by the lexer. */
class StringTable {
protected:
	std::vector<ustring> strings;

	typedef std::unordered_map<ustring, size_t> PrevMap;

	PrevMap cachedLocations;

public:

    /** Clear table state. */
    void clear() {
        cachedLocations.clear();
        strings.clear();
    }

    /**
     * Push a string and return the index.
    */
	size_t pushString(ustring const& s) {
        PrevMap::iterator it = cachedLocations.find(s);
        if (it != cachedLocations.end()){
            return (*it).second;
        }

        size_t idx = strings.size();
        cachedLocations.emplace(s, idx);

        strings.push_back(s);

        return idx;
    }
        
    /**
     * Get an existing string by index.
    */
	ustring const& getString(size_t index)  {
    	return strings[index];
    }
};

/** Stores mappings from logical token names to string representations. */
static std::unordered_map<int, ustring> token_name_map;

/** Stores mappings from logical literal token names to literal values. */
static std::unordered_map<int, ustring> token_literal_value_map;

/**
 * This is the token value passed into the Lemon parser. It always has a type, 
 * but it might not always have a value. This is indicated by having a 
 * nullptr `valueTable`.
 * 
 * It seems Token must be a trivial value type to pass through
 * the lemon parser. This means we need to play tricks with
 * the value string.
 * 
*/
struct Token {
    int type; ///< Numeric type defined by the header 'concat_grammar.h', output by lemon.
    size_t valueIndex; ///< index into the string table where we can find our value.
    StringTable *valueTable; ///< pointer to string table of values, or nullptr if this token has no value.
    int line; ///< line number that the lexer *finished* this token on (sorry)

    /**
     * Get either the regex-matched value for a value token, or just a copy of the
     * literal string for a literal token.
    */
    ustring value() const { 
        if (valueTable) return valueTable->getString(valueIndex);
        return token_literal_value_map[type];
    }

    /**
     * Get the name of this token as a string.
    */
    ustring const& name() const {
        return token_name_map[type];
    }

    /**
     * Get a reasonable, perhaps truncated, string representation of this token.
    */
    ustring toString() const {
        sstream valueStream;
        valueStream << name() << "[line: ]" << line << " <" << value() << ">";
        return valueStream.str();
    }

    int operator~() const { return line; }
};

/** Convenience method to make a token. */
Token make_token(int type, int line) {
    return Token {type, 0, nullptr, line};
}

/** Convenience method to make a token. */
Token make_token(int type, StringTable & st, ustring const& s, int line) {
    return Token {type, st.pushString(s), &st, line};
}


//============================== LEXER IMPLEMENTATION =================================

/**
 * This implements a recursive prefix tree, used to match literals in the lexer.
 * 
 * Nodes with defined values may also define a terminator pattern, which must match in order for the node to match.
*/
template <typename V_T>
struct PTNode {
    uuchar code; ///< character contribution
    std::optional<V_T> value; ///< the output token value if matched
    std::optional<uregex> terminatorPattern; ///< a regex used to check if the literal is properly terminated
    std::vector<PTNode> children; ///< suffixes
    bool isRoot; ///< is this the root node?

    PTNode(uuchar code, std::optional<V_T> const& value, std::optional<uregex> const& terminator, bool isRoot = false) : code(code), value(value), terminatorPattern(terminator), children(), isRoot(isRoot) {}

    /**
     * Recursively add a literal to the tree.
    */
    void add_value(ustring_view const& code, V_T const& value, std::optional<uregex> const& terminator = std::nullopt) {
        if (code.length() == 0) { // all of the previous recursions have matched (or user is adding a null string?)
            if (isRoot // yeah, it was a null string, which won't work and is extremely unlikely coming from the autogen lexer conf
                || this->value) // of we're already set
                    throw std::runtime_error("Attempting to redefine lexer literal " + toExternal(token_name_map[this->value.value()]));
            this->value = value;
            this->terminatorPattern = terminator;
            return;
        }

        for (auto & c : children) {
            if (c.code == code[0]) { // one of our children is further along the chain
                c.add_value(code.substr(1, code.length() - 1), value, terminator);
                return;
            }
        }

        // directly create the child node
        children.emplace_back(code[0], std::nullopt, std::nullopt);

        // and recurse into it with the suffix of the string. If the contracted substring becomes null, that will trigger the child to take the value
        children.back().add_value(code.substr(1, code.length() - 1), value, terminator);
    }

    /** Check the terminator pattern, or return true automatically if we don't have a terminator defined.  */
    bool tryTerminator(ustring::const_iterator const& first, ustring::const_iterator const& last) const {
        if (!terminatorPattern) return true;

        return std::regex_search(first, last, terminatorPattern.value(), std::regex_constants::match_continuous);
    }

    /** Value, and an iterator pointing to the input character immediately following the literal. */
    using LexResult = std::tuple<V_T, ustring::const_iterator>;

    /**
     * Try to match the maximal string possible from the beginning of the input range.
    */
    std::optional<LexResult> tryValue(ustring::const_iterator first, ustring::const_iterator last) const {
        if (children.empty() || first == last) { // we'll never have a null input, so we've reached end of input or end of chain while still matching.
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
        // if we got here naturally, after searching children, it's because there was a match failure on the
        // suffix after this node. If we have a value, check the terminator and return.
        if (value && tryTerminator(first, last)) {
            return std::make_tuple(value.value(), first);
        }

        // no match, or the match is for an internal node with no value.
        return std::nullopt;
    }
};

/** Flags for regex scanning. */
struct RegexScannerFlags {
    const int v = 0;

    static constexpr auto Default = 0;
    static constexpr auto CaseSensitive = 1; ///< This regex should be evaluated with case sensitivity enabled

    operator int() const { return v; }
    RegexScannerFlags(int const& v) : v(v) {}
    RegexScannerFlags() = default;
};


/** Flags for configuring string scanning. */
struct StringScannerFlags {
    int v = 0;

    static constexpr auto Default = 0;
    static constexpr auto SpanNewlines = 1; ///< Strings of this type should allow internal newlines without error
    static constexpr auto JoinAdjacent = 2;  ///< Strings of this type should be joined together _in the lexer_ when only skips occur between them

    operator int() const { return v; }
    StringScannerFlags(int const& v) : v(v) {}
    StringScannerFlags() = default;
};

/** Convert a string into a case-insensitive, ECMA-flavored regex. */
uregex s2regex(ustring const& s, RegexScannerFlags const& flags) {
    auto flagset = std::regex::ECMAScript;
    if (!(flags & RegexScannerFlags::CaseSensitive)) {
        flagset |= std::regex::icase;
    }
    
    return uregex(s, flagset);
}

/**
 * This is a relatively basic lexer. It handles two classes of tokens, plus skip patterns and strings.
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
 * Strings have user-defined delimeters and escapes, and may optionally span newlines.
 * 
*/
struct Lexer {
    static PTNode<int> literals;
    static std::vector<uregex> skips;
    static std::vector<std::tuple<uregex, int>> valueTypes; ///< regex pattern, token code
    static std::vector<std::tuple<uuchar, uuchar, int, StringScannerFlags>> stringDefs; ///< delim, escape, token code, span newlines

    /**
     * Add a literal/constant token, with an optional terminator pattern.
    */
    static void add_literal(int tok_code, ustring const& code, std::optional<ustring> const& terminator = std::nullopt, RegexScannerFlags const& terminatorFlags = RegexScannerFlags::Default) {
        literals.add_value(
                          code, 
                          tok_code, 
                          terminator ? 
                            std::make_optional(s2regex(terminator.value(), terminatorFlags))
                            : std::nullopt);
        token_literal_value_map.emplace(tok_code, code);
    }

    /** Add a skip pattern to the lexer definition. */
    static void add_skip(ustring const& r, RegexScannerFlags const& flags = RegexScannerFlags::Default) {
        skips.push_back(s2regex(r, flags));
    }

    /** Add a value pattern to the lexer definition. */
    static void add_value_type(int tok_code, ustring const& r, RegexScannerFlags const& flags = RegexScannerFlags::Default) {
        valueTypes.push_back(std::make_tuple(s2regex(r, flags), tok_code));
    }

    /** Add a string definition to the lexer definition. */
    static void add_string_def(uuchar delim, uuchar escape, int tok_code, StringScannerFlags flags = StringScannerFlags::Default) {
        stringDefs.push_back(std::make_tuple(delim, escape, tok_code, flags));
    }

    // == instance ==
private:
    ustring input; ///< the entire input string to lex
    ustring::const_iterator curPos; ///< current authoritative position in the string
    StringTable &stringTable; ///< reference to parser string table to use
    int count; ///< count of tokens lexed
    bool reachedEnd; ///< have we reached the end?
    int line = 1; ///< what's our current line?
    
    /** Make a runtime error with context info. */
    std::runtime_error make_error(std::string const& message) {
        char buf[1024];
        snprintf(buf, 1024, "Lexer failure on line %d. %s Around here:\n", line, message.c_str());
        return std::runtime_error(std::string(buf) + toExternal(remainder(100)));
    }

    /** Advance curPos by the given count. */
    siter advanceBy(size_t count) {
        auto oldPos = curPos;
        std::advance(curPos, count);
        line += countLines(oldPos, curPos);

        return oldPos;
    }

    /** Advance curPos to the given position. */
    siter advanceTo(siter const& newPos) {
        auto oldPos = curPos;
        curPos = newPos;
        line += countLines(oldPos, curPos);

        return oldPos;
    }

    /** Count lines between iterators. */
    int countLines(siter from, siter const& to) {
        int lineCount = 0;
        for (; from != to; from++) {
            if (*from == '\n') {
                lineCount++;
            }
        }
        return lineCount;
    }

    /** Repeatedly apply skip patterns, consuming input if they match. */
    void skip() {
        bool skipped = false;
        do { 
            skipped = false;
            for (auto const& r : skips) {
                regex_results results;
                if (std::regex_search(curPos, input.cend(), results, r, std::regex_constants::match_continuous)) {
                    skipped = true;
                    advanceBy(results.length());
                }
            }
        } while (skipped);
    }

    /** Find the end of the string from the given start position. */
    siter stringEnd(uuchar stringDelim, uuchar escape, StringScannerFlags flags, siter stringStart, siter end) {
        for (; stringStart != end; ++stringStart) {
            if (*stringStart == escape) {
                auto nextChar = stringStart + 1;
                if (nextChar == end) goto end_of_input;
                if ((*nextChar == stringDelim) || (*nextChar == escape)) {
                    ++stringStart; // skip past this delim, the loop increment will skip the escaped char
                    continue;
                }
            }
            else if (!(flags & StringScannerFlags::SpanNewlines) && (*stringStart == '\n')) {
                throw make_error("Non-spanning string crossed newline.");
            }
            else if (*stringStart == stringDelim) {
                return stringStart;
            }
        }

        end_of_input:
        throw make_error("String lexing reached end of line.");
    }

    /** Try all of the string definitions and attempt to get a string, returning nullopt if no string is possible. */
    std::optional<Token> nextString() {
        auto n = [this] (int tokCode, uuchar delim, uuchar escape, StringScannerFlags flags) -> std::optional<Token> {
            if (*curPos == delim) { // if we get past this, we're either going to return a string token or exception out.
                auto send = stringEnd(delim, escape, flags, curPos + 1, input.cend());
                auto startLine = line;
                auto sstart = advanceTo(send + 1); // move past the end delim
                return make_token(tokCode, stringTable, ustring(sstart + 1, send), startLine);
            }
            else { 
                return std::nullopt;
            }
        };

        using std::get;

        for (auto const& sdef : stringDefs) {
            if (auto matchedString = n(get<2>(sdef), get<0>(sdef), get<1>(sdef), get<3>(sdef))) {
                auto flags = get<3>(sdef);
                if (flags & StringScannerFlags::JoinAdjacent) {
                    sstream retval;
                    retval << matchedString.value().value();
                    skip();
                    while (auto anotherOne = n(get<2>(sdef), get<0>(sdef), get<1>(sdef), get<3>(sdef))) {
                        retval << anotherOne.value().value(); // get the actual string value out, the first one's for the `optional`
                        skip();
                    }
                    return make_token(matchedString.value().type, stringTable, retval.str(), matchedString.value().line);
                }
                else {
                    return matchedString;
                }
            }
        }

        return std::nullopt;
    }

    /** Query the prefix tree to check for a literal token, returning nullopt if nothing matches. */
    std::optional<Token> nextLiteral() {
        auto result = literals.tryValue(curPos, input.cend());
        if (!result) return std::nullopt;

        advanceTo(std::get<1>(result.value()));
        return make_token(std::get<0>(result.value()), line);
    }

    /** Try all the value patterns to see if one matches, returning it if it does. Returns nullopt if nothing matches. */
    std::optional<Token> nextValue() {
        for (auto const& r : valueTypes) {
            regex_results results;
            if (std::regex_search(curPos, input.cend(), results, std::get<0>(r), std::regex_constants::match_continuous)) {
                auto match_iterator = results.begin();
                if (results.size() > 1) { // skip past the whole match to get a submatch
                    std::advance(match_iterator, 1);
                }

                ustring value = (*match_iterator).str();

                advanceBy(results.length()); // advance by length of _entire_ match
                return make_token(std::get<1>(r), stringTable, value, line);
            }
        }
        return std::nullopt;
    }

public:

    /** Create a new lexer with the given input, using the given string table. */
    Lexer(ustring const& inputString, StringTable & stringTable) : input(inputString), curPos(input.cbegin()), stringTable(stringTable), count(0), reachedEnd(false) {}

    /** 
     * Get the next token. Returns a special EOF token (defined by Lemon) when it 
     * reaches end of input, returns nullopt on the next call after emitting EOF.
     * 
     * @throw std::runtime_error if there's a error lexing.
     * */
    std::optional<Token> next() {
        skip();

        if (consumedInput()) {
            if (reachedEnd) { // second time we return nullopt so we can stop operating
                return std::nullopt;
            }
            else {
                reachedEnd = true; // first time, we emit the EOF token
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

        throw make_error("Cannot lex next character. Not part of any match.");
    };

    /** Get the current line of the current lexer position. */
    int const& getLine() const { return line; }

    /** Has the lexer consumed all input? */
    bool consumedInput() {
        return curPos == input.cend();
    }

    /** Get a portion of the input after the current position, used for error reporting. */
    ustring remainder(size_t len = 0) {
        return ustring(curPos, len && (curPos + len < input.cend()) ? (curPos + len) : input.cend());
    }

    /** Get a count of all tokens lexed. */
    int getCount() const {
        return count;
    }
};

/** Forward declaration of codegen'd lexer initialization function. Defined by the BuildLexer.py */
void _init_lexer();

// static storage for lexer.
PTNode<int> Lexer::literals(0, std::nullopt, std::nullopt, true); // root node.
decltype(Lexer::skips) Lexer::skips;
decltype(Lexer::valueTypes) Lexer::valueTypes;
decltype(Lexer::stringDefs) Lexer::stringDefs;


//========================== PARSER STATE AND INTERNAL TREE ==============================


struct ParseNode;

/**
 * Used to implement syntax sugar inside the grammar actions.
 * 
 * Implicitly converts between PaseNode* and this handle.
*/
struct GrammarActionNodeHandle {
    using ChildrenPack = std::initializer_list<GrammarActionNodeHandle>;
    ParseNode* node;

    GrammarActionNodeHandle() = default;
    
    // implicit conversions
    GrammarActionNodeHandle(ParseNode* const& n) : node(n) {}
    operator ParseNode*() { return node; }

    // sugar
    GrammarActionNodeHandle& operator[](ChildrenPack const& toAppend);
    GrammarActionNodeHandle& operator+=(GrammarActionNodeHandle & rhs);
    int operator~() const;
    //explicit GrammarActionNodeHandle& operator=(Token const& tok); //TODO: need `_` in scope somehow.
};


/** Either a production name or a token value. */
using ParseValue = std::variant<ustring, Token>;

/**
 * A parser-internal parse node.
 * 
 * ParseNodes are handled by pointer within the parser.
*/
struct ParseNode {
    ParseValue value; ///< the production or token
    int64_t line; ///< line for this node
    std::vector<ParseNode*> children; ///< pointers to children

    /**
     * Append a sequence of things that, individually, will
     * convert to ParseNode*.
    */
    template <typename T>
    ParseNode* append(T const& childSeq) {
        for (auto c : childSeq) {
            children.push_back(c);
        }

        return this;
    }

    /** Add a node to the end of the children list. */
    ParseNode* push_back(ParseNode *n) { children.push_back(n); return this; }

    /** Add a node to the beginning of the children list. Not typically recommended. */
    ParseNode* push_front(ParseNode *n) { children.insert(children.begin(), n); return this; }

    /** Add a node to the end of the children list. */
    ParseNode* pb(ParseNode *n) { return push_back(n); }

    /** Add a node to the beginning of the children list. Not typically recommended. */
    ParseNode* pf(ParseNode *n) { return push_front(n); }

    /** Set the line number of this node. */
    ParseNode* l(int64_t line) { this->line = line; return this; }

private:
    friend class Parser;
    friend class std::unique_ptr<ParseNode>;
    ParseNode() = default;
};
/** Used to brace-enclose a list of children for various functions. */
//using ChildrenPack = std::initializer_list<ParseNode*>;
using ChildrenPack = GrammarActionNodeHandle::ChildrenPack;

GrammarActionNodeHandle& GrammarActionNodeHandle::operator[](ChildrenPack const& toAppend) {
    node->append(toAppend);
    return *this;
}

GrammarActionNodeHandle& GrammarActionNodeHandle::operator+=(GrammarActionNodeHandle & rhs) {
    node->children.push_back(rhs);
    return *this;
}

int GrammarActionNodeHandle::operator~() const {
    return node->line;
}

/**
 * Used to implement syntax sugar inside the grammar actions.
*/
struct GrammarActionParserHandle {
    Parser* parser; ///< pointer to the parent parser

    /** Passthrough to make_node. */
    GrammarActionNodeHandle operator()(const char* production, ChildrenPack const& children = {}, int64_t line = -1);
    GrammarActionNodeHandle operator()(ustring const& production, ChildrenPack const& children = {}, int64_t line = -1);
    GrammarActionNodeHandle operator()(Token const& terminal);
    
    /** Passthrough to push_root. */
    GrammarActionNodeHandle operator=(GrammarActionNodeHandle newRoot);

    void drop_node(GrammarActionNodeHandle & toDrop);
    void error();
    void success();
};


/**
 * Implements the parser and all state for a parser run.
*/
class Parser {
    void* lemonParser; ///< opaque poiner to lemon parser

    std::unordered_map<ParseNode*, std::unique_ptr<ParseNode>> allNodes; ///< storage for nodes
    StringTable stringTable; ///< string storage
    Token currentToken; ///< the last token passed from the lexer for parsing 

    ParseNode *root = nullptr; ///< root node for the parse tree
    bool successful = false; ///< have we received the successful message from the parser
    GrammarActionParserHandle thisHandle { this };

    void freeParserObject() {
        if (lemonParser) { // could be non-null if there was an exception.
            LemonPyParseFree(lemonParser, free);
        }
        lemonParser = nullptr;
    }

    void buildParserObject() {
        if (lemonParser) {
            freeParserObject();
        }

        lemonParser = LemonPyParseAlloc(malloc);

        if (!lemonParser) {
            throw std::runtime_error("Cannot allocate memory for parser framework.");
        }
    }

    /**
     * Reset the parser state. Called internally by `parseString()`, so not necessary to call manually.
    */
    void reset() {
        allNodes.clear();
        stringTable.clear();

        currentToken = make_token(0, -1);
        root = nullptr;
        successful = false;

        buildParserObject();
    }

    /**
     * Pass the next token into the lemon parser.
    */
    void offerToken(Token token) {
        currentToken = token;
	    LemonPyParse(lemonParser, token.type, token, thisHandle);
    }


public:

    /** Create a new parser, allocating lemon parser state. */
    Parser() : lemonParser(nullptr), allNodes(), stringTable() {
        _init_lexer();
    }

    /** Deallocate lemon parser state. */
    ~Parser() {
        freeParserObject();
    }


    /** Make a new node. */
    GrammarActionNodeHandle make_node(ParseValue const& value, ChildrenPack const& children = {}, int64_t line = -1) {
        auto node = std::unique_ptr<ParseNode>(new ParseNode); // can't use `make_unique` because the constructor's private.
        node->value = value;
        if (std::holds_alternative<Token>(value)) {
            node-> line = std::get<Token>(value).line;
        }
        else {
            node->line = line;
        }

        //node->children.insert(node->children.end(), children);
        node->append(children);

        auto retval = node.get();
        allNodes.emplace(retval, std::move(node));

        return retval;
    }

    /** Short for make_node. */
    GrammarActionNodeHandle mn(ParseValue const& value, ChildrenPack const& children = {}, int64_t line = -1) {
        return make_node(value, children, line);
    }
    
    /**
     * Set the root node of the parse tree.
    */
    GrammarActionNodeHandle push_root(GrammarActionNodeHandle pn) {
        return root = pn;
    }

    /** 
     * Drop the given node from internal storage. Not strictly necessary, but can keep interim
     * memory usage lower.
     */
    void drop_node(GrammarActionNodeHandle pn) {
        auto it = allNodes.find(pn);
        if (it != allNodes.end()) {
            allNodes.erase(it);
        }
    }

    /**
     * Used by the lemon parser to signal a parse error.
    */
    void error() {
        throw std::runtime_error("Parse error on token: " + toExternal(currentToken.toString()));
    }

    /**
     * Used by the lemon parser to signal a parse success.
    */
	void success() {
        successful = true;
    }

    /**
     * Parse the given input string, returning a parse tree on success.
     * 
     * Invalidates parse nodes returned from any previous invocation of `parseString` on this Parser.
     * 
     * @throw std::runtime_error on lex or parse error.
    */
    ParseNode* parseString(std::string const& input) {
        reset(); // allocates the parser object

        Lexer lexer(toInternal(input), stringTable);

        while (auto tok = lexer.next()) {
            offerToken(tok.value());
        }

        if (!(successful && root)) {
            throw std::runtime_error("Lexer reached end of input without parser completing and setting root node.");
        }

        return root;
    }
};

GrammarActionNodeHandle GrammarActionParserHandle::operator()(const char* production, ChildrenPack const& children, int64_t line) {
    return parser->make_node(toInternal(production), children, line);
}

GrammarActionNodeHandle GrammarActionParserHandle::operator()(ustring const& production, ChildrenPack const& children, int64_t line){
    return parser->make_node(production, children, line);
}

GrammarActionNodeHandle GrammarActionParserHandle::operator()(Token const& terminal){
    return parser->make_node(terminal);
}

GrammarActionNodeHandle GrammarActionParserHandle::operator=(GrammarActionNodeHandle newRoot) {
    return parser->push_root(newRoot);
}

void GrammarActionParserHandle::drop_node(GrammarActionNodeHandle & toDrop) { parser->drop_node(toDrop); }
void GrammarActionParserHandle::error() { parser->error(); }
void GrammarActionParserHandle::success() { parser->success(); }

} // namespace


//========================= PUBLIC API IMPLEMENTATIONS ================================

namespace parser {


/**
 * Create a complete dot graph, rooted at the given ParseNode.
*/
std::string dotify(ParseNode const& pn) {
#ifndef LEMON_PY_SUPPRESS_PYTHON
    py::gil_scoped_release _release_GIL;
#endif
    
    std::stringstream out;

    out << "digraph \"AST\" { \n";
	out << "node [shape=record, style=filled];\n\n";

    pn.dotify(out, nullptr);

    out << "\n}\n";

    return out.str();
}

/**
 * Uplift a node from the internal pointer-based representation into the 
 * external value-semantics representation.
*/
ParseNode uplift_node(_parser_impl::ParseNode* alien, int & idCounter) {
    using _parser_impl::toExternal;
    ParseNode retval;
    retval.id = idCounter++;

    if (std::holds_alternative<_parser_impl::Token>(alien->value)) {
        auto tok = std::get<_parser_impl::Token>(alien->value);
        retval.tokName = toExternal(tok.name());
        retval.value = toExternal(tok.value());
    }
    else {
        retval.production = toExternal(std::get<_parser_impl::ustring>(alien->value));
    }
    retval.line = alien->line;
    
    for (auto c : alien->children) {
        retval.children.push_back(uplift_node(c, idCounter));
    }

    return std::move(retval);
}

/**
 * Uplift a node from the internal poiner-based representation into the 
 * external value-semantics representation.
*/
ParseNode uplift_node(_parser_impl::ParseNode* alien) {
    int idCounter = 0;
    return uplift_node(alien, idCounter);
}

/**
 * Parse a string and return a value-semantics parse node.
 * 
 * @throw std::runtime_error if there is a lex or parse error.
*/
ParseNode parse_string(std::string const& input) {
#ifndef LEMON_PY_SUPPRESS_PYTHON
    py::gil_scoped_release _release_GIL;
#endif

    using namespace _parser_impl;
    Parser p;
    return uplift_node(p.parseString(input));
}

} // namespace parser

#ifndef LEMON_PY_SUPPRESS_PYTHON
PYBIND11_MODULE(PYTHON_PARSER_MODULE_NAME, m) {
    m.def("parse", &parser::parse_string, "Parse a string into a parse tree.", py::return_value_policy::move);
    m.def("dotify", &parser::dotify, "Get a graphviz DOT representation of the parse tree.");

    py::class_<parser::ParseNode>(m, "Node")
    .def(py::init<>())
    .def("__repr__", [](parser::ParseNode const& pn) { return py::str(pn.toString()); }, "Get an approximation of the representation.", py::return_value_policy::take_ownership)
    .def("__getitem__", [](parser::ParseNode const& pn, size_t item) -> parser::ParseNode const& { return pn[item]; }, "Get a child by index.", py::return_value_policy::reference_internal)
    .def("__iter__", [](parser::ParseNode const& pn) { return py::make_iterator(pn.begin(), pn.end(), py::return_value_policy::reference_internal); }, "Children iterator.")
    .def("__len__", [](parser::ParseNode const& pn) { return pn.childCount(); }, "Get number of children.")
    .def("as_dict", &parser::ParseNode::asDict, "Make a deep copy of this node and all children to a dictionary representation. `.attr` is ref-copied, but not deep-copied. ", py::return_value_policy::take_ownership)
    .def(py::self == py::self)
    .def(py::self != py::self)
    .def_property_readonly("production", &parser::ParseNode::getProduction, "Get production if non-terminal.", py::return_value_policy::take_ownership) // these return copies of strings
    .def_property_readonly("type", &parser::ParseNode::getToken, "Get type if terminal.", py::return_value_policy::take_ownership)
    .def_property_readonly("value", &parser::ParseNode::getValue, "Get value if terminal.", py::return_value_policy::take_ownership)
    .def_readonly("line", &parser::ParseNode::line, "Line number of appearance.")
    .def_readonly("c", &parser::ParseNode::children, "Children.", py::return_value_policy::reference_internal)
    .def_readonly("id", &parser::ParseNode::id, "ID number for this node (unique within tree).")
    .def_readonly("attr", &parser::ParseNode::attr, "Free-use attributes dictionary.");
}
#endif


#ifndef LEMON_PY_SUPPRESS_PYTHON
#include <concat_grammar.h>
#else // this next line is used by the C++ codegen aspect to inline token macro definitions
struct _to_be_replaced{};
#endif
