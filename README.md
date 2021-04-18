Lemon Py
--------

This project wraps the Lemon parser generator. If you aren't sure what
a parser generator is, this is maybe not something you need.

lemon-py provides functions that compile a EBNF grammar and a lexer
definition into a native/extension module for Python 3.x. The
resulting parser module has no external dependencies (including on
this project) and is suitable for use as a submodule in other
projects. This project _itself_ has several dependencies, including a
standard GNU native build chain.

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


Motivation and Alternatives
---------------------------

I want to play with definitions and implementations of programming
languages. It's hard enough to invent an unambiguous, expressive
syntax without then needing to reconfigure it to avoid parser
generator limitations. Since language definitions are often deeply
recursive, you can spend quite a lot of work before you discover some
parse ambiguity that necessitates a restructuring of a large part of
your definition. This especially crops up in the deeply-recursive
aspects of programming languages like expression syntaxes (`5 + max(2,
some_func()) / 6.2)`), at which point controlling operator and
production precedence becomes critical.

Literally every time I have tried an LL or PEG parser generator on any
sufficiently complicated grammar, I have run into some ridiculous
precedence or ambiguity issue that has to be solved by rewriting some
big chunk of my expression tree into something substantially less
maintainable. It becomes complicated to explore "what if?" questions
about syntax because adding another branch to a rule often requires
rewriting many other rules to take the new union production. Perhaps
smarter language authors have no trouble with this, but I do.

Lemon accepts relatively "normal" EBNF grammars, has an outstanding
grammar action system, and critically implements a LALR(1)
parser. Furthermore, it implements a full suite of associativity and
precedence rules for terminals. Finally, unlike `bison`/`Yacc` output,
the Lemon parser code is safe by default for multiple instantiation
with no state shared between instances--I also prefer Lemon's overall
syntax to `bison`. In Lemon, I can simply write down the grammar for
my language and resolve ambiguities abstractly instead of by coercing
the grammar itself.

At the same time, Lemon is a fairly old project (started in the 80's)
and has a relatively naive API. While it's suitable for directly
generating an AST with appropriate types and grammar rules, it works
only with "trivial" types, provides no default/generic parse tree
data, and it requires an external, proper lexer. As a result, when
working with it in the past, I have been inclined to develop the
target language's custom lexer and AST type framework at the same time
as the parser grammar. Since Lemon works only with raw pointers (or
trivial value types), this inclines the AST design toward owning raw
pointers, which feels pretty gross here in 2021.

Likewise, since I'm rarely targeting general-purpose languages where
translation units become enormous, nor am I often targeting machine-
code back ends where bit-twiddling and isomorphic binary
representation are routine, and I don't need blinding performance...
implementing the whole of my compiler in C++ often feels onerous. I
want my favorite parser available from Python, with the option of
using the same grammar in C++ if I'm ready to build a native compiler.

There exist other LR or LALR parser generators for Python:

* [Lark](https://github.com/lark-parser/lark)
* [lrparsing](https://pypi.org/project/lrparsing/)
* [pyleri](https://github.com/transceptor-technology/pyleri)

You may like one of those better. Certainly they have advantages, like
skipping the rather time-consuming C++ compilation stage. The latter
two also build their grammars mostly or entirely from within Python
code, which can make them attractive for certain applications. I do
not like that approach, as I find reading and following the recursion
of different rules much more complicated when cluttered by the Python
syntax on top of the grammar's own definition syntax. You might like
it better than writing C++ grammar actions, though.

Lark is probably the closest in capabilities and design to lemon-py,
in that it is driven by a data file (string) defining the grammar. It
also has a built-in lexer. However, its implementation of LALR(1)+EBNF
does not support operator associativity or precedence, meaning that
despite the algorithm it uses, it functionally has many of the same
shortcomings as typical LL and PEG parser generators.

Of those options, only `pyleri` is capable of outputting a parser
generator in C.


Prereqs
-------

Python 3.6+ required.

lemon-py depends on `g++` supporting at least c++17. It also depends
on the `pybind11` PyPi module (not just headers installed to system
include paths).

Start by building lemon with `make` in the root directory. If you
don't do this, later commands will fail in mysterious and bizarre
ways.

You can install via `setup.py` and `pip3`, or just add `src` to your
`PYTHONPATH`. [Note: That first part is currently lies. There's no
setup. Hack your `PYTHONPATH`.]


Invocation
----------

Build a grammar into a loadable module and place it in your personal
site-packages path using the following command:

`python3 -m lemon_py.BuildGrammar path/to/grammar.lemon`

You can then load it with `import MOD_NAME`, where `MOD_NAME` is
whatever you configure with the `@pymod` directive inside the grammar
file.

You can also use `python3 -m lemon_py.Driver MOD_NAME path/to/input`
to parse a file and run some basic operations on the resulting parse
tree. One such option is `--vis`, which will run graphviz `dot` and
ImageMagick `display` to visualize the parse tree (assuming those
tools are installed).


Paser Module API
----------------

The generated module exports a `parse(input: str) -> ParseNode`
function that parses a string into a parse tree. Lex and parse errors
generate `RuntimeError` with text describing the error and location.

The parse tree is represented by a module-defined class implemented in
C++, with name `ParseNode`. You can use the usual `help` and `dir`
functions to explore it.

`ParseNode` instances have read-only `.production`, `.type`, `.value`,
`.line`, and `.c` members. `.line` contains line number information
(or `-1` if unkown/unset), and line counts start from `1` (not
`0`).

`.c` is the list of this node's children.

`.production` is a user-defined (in the grammar action) string for
non-terminal (internal) nodes. It has a `None` value for terminal
(leaf) nodes.

For terminal (leaf) nodes, `.type` contains a string identical to the
token name used in the Lemon grammar. For value-typed tokens, `.value`
contains the value for that token--note that this is a string of the
characters matched for the token, as no internal type conversions are
performed. Both `.type` and `.value` will be `None` for non-terminal
(internal) nodes; and `.value` will be none for literal-type tokens.

The module also exports a `dotify(input: ParseNode) -> str`
function. This returns a string representing the parse tree and its
values, suitable for rendering using GraphViz `dot`. Note that this
function does not call or depend on GraphViz internally, doing all its
processing as raw text. GraphViz is only needed to interpret the
output.


Module Definition
_________________

lemon-py grammar definitions must include a `@pymod MODULE_NAME`
directive somewhere indicating the Python `MODULE_NAME` to
generate. This text is not removed before processing, and so should be
inside a comment. Example:
       * `//@pymod parse_my_lang`


Lexer Definitions
-----------------

An integrated lexer is one of lemon-py's labor-saving
features. Without it, you would need to write a lexer for each target
language. For the purposes of this (and only this) section, words like
"returned" and "value" should be taken in the context of the internal
lexer->parser interface used inside of grammar production
rules.

Lexer definitions appear inside a comment block as a contiguous run of
`token : pattern` definitions, one per line, starting on the line
following `@lexdef` and continuing until `@endlex`. Note that these
lines are _not_ removed from the grammar input to `lmeon`, and so
should always be inside a comment block. It's an error, resulting in
mysterious downstream problems, to define multiple `@lexdef`
blocks. Do it once; do it right.

The lexer itself is relatively basic. It handles two classes of tokens
("literal" and "value"), plus user-defined skip patterns and
pre-defined string support. Declarations of different classes may be
mixed in the `@lexdef` block, and the ordering rules discussed below
apply only to the relative definition of different tokens of the _same
class_.

"literal" tokens are defined by a fixed string of characters, and are
stored/matched with a basic prefix tree. Literals are matched
greedily, with the longest matching sequence having priority. Thus the
order in which you define overlapping literals is largely irrelevant
(although definition order will influence search order between
disjoint literals). Literal tokens are returned by Lemon-defined code
number, without an additional text value.

Literal syntax: `TOKEN := literal_string`.
 
"value" tokens are defined by a regular expression, and are returned
with both a token code and a text value. A single sub-match may be
specified, and if present will denote a partial value to be extracted
from the overall token match [note: this could be expanded with
relative ease to permit specification of a particular match within the
pattern]. No type conversions are done, all values are returned as
strings.

Value token patterns are checked in the same order they are defined
with `add_value_type`, which is the same order they're specified in
the `@lexdef` block. Typically this means you should specify longer,
more specific patterns first; and then shorter, or more generic
patterns after. For instance, to keep your `INT_LITERAL` from always
eating the first part of a `FLOAT_LITERAL` (resulting in subsequent
lex or parse errors), define the floating-point literal (with
mandatory decimal point) _before_ the integer literal.

NOTE: All regular expressions are currently matched *without* case
sensitivity.

Value syntax: `TOKEN : regular_expression`.
 
Skip patterns are simply regexes that are used to skip whitespace,
comments, or other lexically and syntactically-irrelevant
content. Skip patterns are applied before every attempt at token
extraction. The `reminder_name` below is not used internally, and can
simply be used to label the skip pattern for human comprehension.

NOTE: All regular expressions are currently matched *without* case
sensitivity.

Skip syntax: `!reminder_name : regular_expression`.

Strings are handled by a set of internal functions that provide for
escaping the delimeter. They're disabled by default. When enabled by
defining the attached token code, they are delimited _only_ by single
or double quotes (respectively), and are only configurable in which
token code they are attached to. The escape character is the
backslash, and is not configurable. [Note: this stuff could be easily
made configurable if the `@lexdef` language were enhanced to support
the definition.]

String syntax:
       * `" := STRING_TOK`
       * `' := CHAR_TOK`

Token classes are tested in the following order:
      0. (skips)
      1. strings
      2. literals
      3. values

Skips are applied repeatedly, before checking for the next lexical
token, until no skip consumes input.

[TODO: this ordering currently means that literals will consume the
beginnings of e.g. identifiers. This isn't ideal. I should probably
add an "ends with whitespace" option for literals that might look like
part of some other token.]


Parser Definition
-----------------

Check out the examples for a better idea of how to use this
stuff. It's not especially intuitive if you've never used a parser
generator before, and teaching you to use one is beyond the scope of
this readme. I suggest the Dragon Book (you know the one), or any of
the various tutorials on using Bison/Yacc. The concepts are highly
transferable. 

The `test_grammars/parasol` directory contains a draft of the grammar
for a fairly "complete" programming language, including a non-trivial
expression tree. This goes well beyond the usual toy examples provided
with parser generators. One real-world "wrinkle" to note is the use of
the lexer to detect function calls and "scope refs" instead of the
parser (by id with trailing `(` and `[` respectively), thereby
avoiding ambiguities around function argument lists inside expression
trees.

The actual Lemon parser-definition language is unchanged, as the
`lemon` source code and `lempar.c` template are completely unchanged
from the sqlite release. Instead, a standard set of headers is
included providing definitions available for use inside the parser
actions. These definitions make it relatively trivial to generate
generic parse trees to represent anything Lemon can parse.

A variable `_parser_impl::Parser *p` is available in every parser
action. This forms the handle to most of the interface. Call the
functions below like `p->mn(...)`.

Available functions are as follows:

* `ParseNode* Parser::push_root(ParseNode *root)`
* `ParseNode* Parser::mn(ParseValue const& value, ChildrenPack const& children = {}, int64_t line = -1)`
* `ParseNode* ParseNode::pb(ParseNode *child)`

`push_root` sets the overall result for the Parser parse operation. If
you don't `push_root` some node (usually in the top level rule), no
results will be generated from parsing. Returns the newly-set root
node.

`mn` stands for `make_node`, and creates a new ParseNode. You can pass
in either a Token or a string production name for the first value; an
initializer list of `ParseNode*` children for the second argument; and
a line number to associate with the node. If you pass a Token for the
first argument, the line is automatically filled in for you; for
non-terminal productions, you'll need to assign the node its line
manually. Returns the newly-created node.

`pb` stands for `push_back` and adds a child to right of a node's
existing children. Note that this function is defined on the
`ParseNode`, not on the `Parser`. Returns the _containing_ (not child)
node.


How Does It Work?
-----------------

Most of the moving parts are in `ParserImpl.cpp`, which implements the
lexer, parse tree, and Python module interface. A small amount of code
(`void _init_lexer(){...}`) is generated to configure the lexer from the
input file, but this is merely appended to the verbatim
`ParseImpl.cpp` file.

A number of seemingly-weird decisions in the C++ widgetry are the
result of adapting to the Lemon grammar action
environment. Critically, Lemon puts the token type and the type of
every production rule into a single giant C `union`. This means they
must be trivial types, which eliminates the direct use of smart
pointers or containers to manage memory within grammar
actions. Instead, the lemon-py `_parser_impl::Parser` object
internally allocates nodes into `unique_ptr`s and returns a raw
pointer to the same object for use within the Lemon grammar
actions. Value tokens are treated similarly, with an integer
terminal-code value member, but pointer and index into a table instead
of an internal `std::string`. When the `Parser` is destructed, all of
the node and string memory is reclaimed and all
`_parser_impl::ParseNode` and `Token` objects created by that `Parser`
become invalid.

Before returning the parse tree to the Python interface, and before it
is destroyed (of course), the entire pointer-based internal parse tree
is dereferenced and copied into a simplified, value-typed
representation (`parser::ParseNode`). The intermediate, indirect tree
is deallocated and the returned tree is free of any complicated
ownership semantics.

The python aspect of lemon-py reads the grammar file, builds the lexer
configuration, wraps the impl code into an `%include{}` block, and
generates a new, complete Lemon input file with all of the code
inlined before the grammar at the bottom. By this point, everything
for the entire python module definition, except for the `pybind11`
headers, is in that one file.

The file is placed in a temporary working directory and `lemon` is
invoked on it to generate the final source code for the parser
module. The module is then built with standard pybind11 settings,
using `g++`. Finally, it is copied into the current `python3`
interpreter's personal site-packages for the current user.


Limitations
-----------

The biggest limitation is probably that the parsers generated by
lemon-py operate on in-memory strings, with no support for incremental
reads from irreversible streams. This vastly eases implementation of
the lexer, which can look ahead an arbitrary number of characters
without side effect as it tests different lexical matches. 

The `pybind11` wrapper is also liable to _copy_ the input string from
Python into a `std::string` before passing it to the native `parse()`
function. From a performance perspective, this is effectively
invisible compared to Python performance, but the redundant memory
usage does mean that lemon-py parsers are probably not suitable for
parsing really huge inputs that challenge system memory
limits.

Enhancing lemon-py to operate on irreversible iterators would require
substantial modification to the lexer, with implications on the ease
of defining a new lexer. I don't currently need such a thing, and I
don't have the energy to implement it just for completeness' sake.

The same goes for the output, which is a value-typed tree full of
(potentially-redundant) strings. For very large inputs, and especially
for deeply-nested non-terminal productions (complicated grammars), the
parse tree itself can be very large. In practice, for most industry
DSLs, hobby languages, and research languages, this is not a real
limitation. Inputs are rarely 1 MiB of text, let alone the GiB it
would take to challenge most modern computers.

These issues can be alleviated by re-using the proven Lemon grammar
with a custom (buffering) lexer, grammar actions, and/or tree type
when moving from typical usage to large-scale usage. The biggest gains
come from rewriting with a high-level AST, built directly in the
grammar actions, which can collapse many intermediate parse nodes into
fewer syntax nodes.

Finally, in the memory hog department, I use recursive functions all
over the place. If you have really recursive grammars or extremely
long lexer literals, you might run into the stack limit on your
machine. I've been programming for 25+ years now have have _only_ run
out of stack when there's been an error causing infinite recursion.

lemon-py is also probably unsuitable for parsing binary or unicode
streams. The underlying Lemon parsers are fully capable of working
with anything you can tokenize, but the lemon-py lexer implementation
is basically character-oriented and currently limited to 8-bit ASCII
text. Support for unicode could be accomplished with relatively
straightforward enhancements to the lexer, but I don't know about
`std::regex` on binary strings.


Anticipated Questions
---------------------

Q. Windows support?

A. I have no idea how to easily, automatically compile a Python
extension on Windows. I think we would need to output a VS project,
and that's more than I want to deal with. Especially because I haven't
written code on Windows in years.


Q. OSX support?

A. Should be easily enough to make that work. Please submit a PR.


Q. Why C++ instead of C?

A. I like modern C++. I don't really like solving the same problems in
boilerplate C.


Q. Why C++17 instead of C++(`n < 17`)?

A. I like modern C++. I don't really like solving the same problems in
antique C++.


Q. Why C++17 instead of C++(`n > 17`)?

A. System compiler support.


Q. Why hardcode to `g++` instead of configurable compiler?

A. It's good enough for me, and I don't need configurability as my
system Python is built with `gcc`. I did look for a cross-platform
compiler control library, but couldn't find one. Let me know if you
have one. Maybe as part of SCons?


Q. Can I use the native lexer+tree+parser framework independently of
Python?

A. Yes, although the process is currently a bit ugly and manual. Use
the `--debug` option when building the grammar to generate all
intermediate and output files to the current working directory
(instead of a tempdir). You can use `concat_grammar.c` (compiled as
C++) as the basis for your native parser.

`#define LEMON_PY_SUPPRESS_PYTHON` will disable inclusion of
`pybind11` and definition of python-related interfaces. The C++
backing for these interfaces is suitable for use in native code, and
may be found in the `parser` namespace. Some implementations may wish
to write their own `parse()` driver that avoids the calls to
`uplift()`, which convert from `_parser_impl` Lemon-compatibility
types into application-facing value types. Keep in mind that the raw
pointers holding together the `_parser_impl::ParseNode` graph do not
own the `ParseNode`s, but rather the `Parser` that generated them owns
the objects--nodes may not outlive their parent `Parser`.

Extracting a suitable header and avoiding multiple definition is left
as an exercise to the reader.


