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

#pragma once

#include <string>
#include <sstream>

#ifndef LEMON_PY_SUPPRESS_PYTHON
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>
namespace py = pybind11;
#else
/** When python is suppressed, stubs out the `dict` definition used to hold parse node attributes. */
namespace py { using dict = void*; }
#endif

namespace parser {

#ifndef LEMON_PY_SUPPRESS_PYTHON
/** Get a string value or None. */
inline 
py::object string_or_none(std::optional<std::string> const& v) {
    if (!v) {
        return py::none();
    }
    else {
        return py::str(v.value());
    }
}
#endif


/**
 * Sanitize a string for dot.
*/
inline 
std::string sanitize(std::string in) {
    auto clean = [&in] (char c, const char* replace, int skipForward = 0) {
        size_t res = 0;
        while((res = in.find(c, res)) < std::string::npos) {
            in.erase(res, 1);
            in.insert(res, replace);
            res += skipForward;
        }
    };

    clean('&', "&amp;", 1);
    clean('"', "&quot;");
    //clean('\'', "&apos;"); // apparently dot doesn't care about this?
    clean('<', "&lt;");
    clean('>', "&gt;");
    //clean('\n', "<br>");

    return std::move(in);
}

/**
 * A value-typed parse node (in contrast to the indirect, pointer-based parse tree used internally).
*/
struct ParseNode {
    std::optional<std::string> production; ///< the production name, if an internal node
    std::optional<std::string> tokName; ///< the token name, if a terminal node
    std::optional<std::string> value; ///< the token value, if a value token
    int64_t line; ///< line number for this node. -1 if unknown.
    std::vector<ParseNode> children; ///< all the children of this parse node
    int id; ///< id number, unique within a single tree
    py::dict attr; ///< if python is enabled, this is a dictionary to contain attributes added by a python transformer

    ParseNode() : production(), tokName(), value(), line(-1), children(), id(-1), attr() {}
    ParseNode(ParseNode && o) noexcept : production(std::move(o.production)), tokName(std::move(o.tokName)), value(std::move(o.value)), line(o.line), children(std::move(o.children)), id(o.id), attr(std::move(o.attr)) {
        o.id = -1;
    }

    ParseNode& operator=(ParseNode && o) noexcept {
        using namespace std;
        production = move(o.production);
        tokName = move(o.tokName);
        value = move(o.value);
        line = o.line;
        children = move(o.children);
        id = o.id;
        o.id = -1;
        attr = move(o.attr);

        return *this;
    }

    ParseNode(ParseNode const& o ) = delete;
    ParseNode& operator=(ParseNode const& o) = delete;

#ifndef LEMON_PY_SUPPRESS_PYTHON

    py::object getProduction() const {
        return string_or_none(production);
    }

    py::object getValue() const {
        return string_or_none(value);
    }

    py::object getToken() const {
        return string_or_none(tokName);
    }

    py::dict asDict() const {
        py::dict myDict;
        myDict["production"] = getProduction();
        myDict["type"] = getToken();
        myDict["value"] = getValue();
        myDict["id"] = id;
        myDict["line"] = line;
        myDict["attr"] = attr;

        auto childList = py::list();
        for (auto const& c : *this) {
            childList.append(c.asDict());
        }
        myDict["c"] = childList;

        return myDict;
    }

#endif

    /**
     * Return a halfway reasonable string representation of the node (but not its children).
    */
    std::string toString() const {
        char outbuf[1024]; // just do the first 1k characters
        if (production) {
            snprintf(outbuf, 1024, "{%s} [%lu]", production.value().c_str(), children.size());
        }
        else {
            snprintf(outbuf, 1024, "%s <%s>", tokName.value().c_str(), value.value().c_str());
        }

        return std::string(outbuf);
    }

    /**
     * Add this node and its children to the dot graph being built up in `out`.
    */
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

    /**
     * Get a particular child node.
    */
    ParseNode const& operator[](size_t index) const {
        if (index >= children.size()) {
            throw std::runtime_error("Child index out of range.");
        }
        return children[index];
    }

    using iter_type = decltype(children)::const_iterator;

    /**
     * Get children iterator.
    */
    iter_type begin() const {
        return children.cbegin();
    }

    /**
     * Get end of children vector.
    */
    iter_type end() const {
        return children.cend();
    }

    /**
     * Number of children of this node.
    */
    size_t childCount() const {
        return children.size();
    }

    /**
     * Checks for syntactic equality. Two nodes are equal if their
     * productions, token name, and value are identical; as well
     * as all their children being equal under this same definition.
     * 
     * This check is recursive.
    */
    bool operator==(ParseNode const& o) const {
        if (&o == this) return true; // we're always equal to ourselves.

        if (childCount() != o.childCount()) return false; // order these checks from cheapest to most expensive
        if (tokName != o.tokName) return false;
        if (production != o.production) return false;
        if (value != o.value) return false;

        for (auto myC = begin(), oC = o.begin(); myC != end() && oC != o.end(); ++myC, ++oC) {
            if (*myC != *oC) return false;
        }

        return true;
    }

    bool operator!=(ParseNode const& o) const {
        return !(*this == o);
    }

};

/**
 * Parse a string and return a parse tree.
 * 
 * @throw std::runtime_error if there is a lex or parse error.
*/
ParseNode parse_string(std::string const& input);

/**
 * Create a complete dot graph, rooted at the given ParseNode.
*/
std::string dotify(ParseNode const& pn);

} // namespace parser