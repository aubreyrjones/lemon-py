Lemon Py
--------

This project wraps the Lemon parser generator. If you aren't sure what
a parser generator is, this is maybe not something you need.

lemon-py provides functions that compile a EBNF grammar and a lexer
definition into a native/extension module for Python 3.x. The
resulting parser module has no external dependencies (including on
this project) and is suitable for use as a submodule in other
projects. This project _itself_ has several dependencies.

lemon-py parsers output a uniformly-typed parse tree. All productions
are identified by a string name, and all terminal values are returned
by string as well--no type conversions are applied inside of the
parser module.

lemon-py grammar files are essentially just regular [lemon grammar
files](https://www.sqlite.org/src/doc/trunk/doc/lemon.html#syntax) but
include an extension to support automatic lexer generation, and a
standardized C++ parse node implementation to build the returned parse
tree. The input file is transformed, lexer definitions extracted, and
then output as a pybind11-based C++ Python extension. This extension
is built and installed in your local sitepath as a python module that
you can just `import`.


Prereqs
-------

Python 3.6+ required.

lemon-py depends on `g++` supporting at least c++17. It also depends
on the `pybind11` PyPi module (not just headers installed to system
include paths).

Start by building lemon with `make` in the root directory.

You can install via `setup.py` and `pip3`, or just add `src` to your
`PYTHONPATH`.



Invocation
----------

Build a grammar into a loadable module and place it in your
site-packages path using the following command:

`python3 -m lemon_py.BuildGrammar path/to/grammar.lemon`

You can then load it with `import MOD_NAME`, where `MOD_NAME` is
whatever you configure with the `@pymod` directive inside the grammar
file.

You can also use `python3 -m lemon_py.Driver MOD_NAME path/to/input`
to run the parser and run some basic operations on the resulting parse
tree. One such option is `--vis`, which will run graphviz `dot` to
visualize the parse tree (assuming `dot` is installed).


Paser Module API
----------------

The generated module exports a `parse(input: str)` function that
parses a string into a parse tree. The parse tree is represented by a
module-defined class implemented in C++. You can use the usual `help`
and `dir` functions to explore it.

The module also supports a `dotify(input: ParseNode)` function. This
returns a string representing the AST, suitable for rendering using
`dot`.


Lexer Definitions
-----------------

Lexer definitions start inside a comment block, on the line following
`@lexdef` and continue until `@endlex`. Note that these lines are
_not_ removed from the grammar input to `lmeon`, and so should always
be inside a comment block.

The lexer is relatively basic. It handles two classes of tokens,
plus skip patterns and strings.

"literal" tokens are defined by a fixed string of characters, and are
stored in a basic prefix tree (PTNode above). These are matched
greedily, with the longest matching sequence having priority. Literal
tokens are returned by lemon-defined code number, without a value.

Literal syntax: `TOKEN := literal_value`.
 
"value" tokens are defined by a regular expression, and are returned
with both a code and a value. A single sub-match may be used to denote
a partial value extraction from the overall token match. No type
conversions are done, all values are strings.

Value token patterns are checked in the same order they are defined
with `add_value_type`.

Value syntax: `TOKEN : regular_expression`. 
 
Skip patterns are simply regexes that are used to skip whitespace,
comments, or other lexically and syntactically-irrelevant
content. Skip patterns are applied before every attempt at token
extraction.

Skip syntax: `!reminder_name : regular_expression`.

Strings are handled by a set of internal functions that handle
escaping. They're disabled by default. When enabled, they are
delimited _only_ by single or double quotes (respectively), and are
only configurable in which token value they are attached to. The
escape character is the backslash.

String syntax:
       `" := STRING_TOK`
       `' := CHAR_TOK`


Parser Environment
------------------

Check out the examples for a better idea of how to use this
stuff. It's...not intuitive.

The actual Lemon parser-definition language is unchanged. However, a
standard set of headers and definitions is available for use inside
the parser actions to make parse-tree construction much easier.

A variable `_parser_impl::Parser *p` is available in every parser
action. This forms the handle to most of the interface.

Available functions are as follows:

* `ParseNode* Parser::push_root(ParseNode *root)`
* `ParseNode* Parser::mn(ParseValue const& value, ChildrenPack const& children = {}, int64_t line = -1)`
* `ParseNode* ParseNode::pb(ParseNode *child)`

`push_root` sets the overall result for the Parser parse operation. If
you don't `push_root` some node (usually in the top level rule), no
results will be generated from parsing.

`mn` stands for `make_node`, and creates a new ParseNode. You can pass
in either a Token value or a string production name for the first
value, an initializer list of `ParseNode*` children, and a line
number to associate with the node.

`pb` stands for `push_back` and adds a child to an existing node.


How Does It Work?
-----------------

Oh my, who knows?

