#include <memory>
#include <vector>
#include <optional>
#include <regex>
#include <tuple>

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

    std::optional<V_T> const& tryValue(std::string::const_iterator first, std::string::const_iterator last) {
        if (first + 1 != last) {
            for (auto const& c : children) {
                if (*first == c.code) {
                    return c.tryValue(first + 1, last);
                }
            }
        }

        if (*first == code) {
            return value;
        }

        return std::nullopt;
    }
};

class Token {};

struct Lexer {
    static PTNode<int> literals;
    static void add_literal(int tok_value, std::string const& code) {
        literals.add_value(code, tok_value);
    }

    static std::vector<std::regex> skips;
    static void add_skip(std::regex const& r) {
        skips.push_back(r);
    }

    static std::vector<std::tuple<std::regex, int>> valueTypes;
    static void add_value_type(int tok_value, std::regex const& r) {
        valueTypes.push_back(std::make_tuple(r, tok_value));
    }

    // == instance ==

    std::string input;
    std::string::const_iterator curPos;
    std::string::iterator lookaheadPos;

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
        return std::nullopt;
    }

    std::optional<Token> nextValue() {
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


PTNode<int> Lexer::literals(0, std::nullopt);