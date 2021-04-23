Lemon Py
--------

[Project Homepage](https://github.com/aubreyrjones/lemon-py/)

* [Prereqs](#Prereqs)
* [Invocation](#Invocation)
* [Generated Parser API](#Parser-Module-API)
* [Module Directive](#Module-Definition)
* [Lexer Definition](#Lexer-Definitions)
* [Parser Definition](#Parser-Definition)
* [C++](#C)

Features

  * LALR(1) parsing
  * configurable lexing
  * generic parse tree
  * grammar action micro-DSL
  * Python and C++17 interfaces


# Overview

This project augments the Lemon parser generator with a high-level
parse tree interface, grammar action DSL, and an integrated,
configurable lexer allowing the creation of an entire standalone,
object-oriented parser from a single input grammar file. The entire
parser is written in native C/C++, and the parser interface is made
comfortably available to both C++ and Python3 applications.

If you're unsure what this is for and if you need it, this is the
first piece that a software compiler uses to understand input code. On
its own, it is not a compiler or parser for any language, but allows
you to (more) easily create parsers for use in your own projects.

lemon-py provides facilities to compile a EBNF grammar and a lexer
definition into a standalone parser. The resulting lexer+parser has
has no external dependencies (including on this project or any Python
code) and is suitable for use as a submodule in other projects.  You
can just ship it, `import` it, and run `parse()`. C++ users might like
to read more about how to [include the parser in a native
project.](#C)

lemon-py parsers output a uniformly-typed parse tree of `ParseNode`
objects. All nonterminals are identified by a string name, and all
terminal values are returned by string as well--no type conversions
are applied inside of the parser module. One standout feature,
available in both Python and C++, is (dependency-free) GraphViz `dot`
output for graphical visualization of parse trees--essential for
language development.

lemon-py extends the Lemon parser definition format with a built-in,
configurable lexer. The lexer handles commonly-encountered token types
such as constant/literal character strings; regular expressions with
optional case sensitivity and/or captured submatch; and configurable
strings. The lexer also supports user-defined "skip" patterns for
omitting lexically-irrelevant characters. The lexer is not especially
performant, but it is intentionally implemented in a straightforward
manner that can be easily understood and extended.

lemon-py grammar files are essentially just regular [lemon grammar
files](lemon/lemon.html) but include an extension to support automatic
lexer generation.

A lexer and grammar definition for lemon-py.

```
/*
expressions.lemon

@pymod expr_parse

@lexdef
!whitespace : \s+
!comment : //.*\n

' ' \   := CHAR
' " \ ! := STRING

ADD := +
SUB := -
MUL := *
DIV := /
L_PAREN := (
R_PAREN := )
COMMA := ,

FLOAT_LIT : [0-9]+\.[0-9]+
INT_LIT : [0-9]+
FNCALL : ([_a-z][_a-z0-9]*)\s*\(
IDENT : [_a-z][_a-z0-9]*
@endlex
*/

// token association, and implicit (ascending) priority
%left COMMA FNCALL.
%left ADD SUB.
%left MUL DIV.

toplevel ::= expr(c1).                            { _ = c1; }

expr(e) ::= expr(c1) ADD(o) expr(c2).             { e = _("+", {c1, c2}, ~o); }
expr(e) ::= expr(c1) SUB(o) expr(c2).             { e = _("-", {c1, c2}, ~o); }
expr(e) ::= expr(c1) MUL(o) expr(c2).             { e = _("*", {c1, c2}, ~o); }
expr(e) ::= expr(c1) DIV(o) expr(c2).             { e = _("/", {c1, c2}, ~o); }
expr(e) ::= SUB expr(c1). [MUL]                   { e = _("neg", {c1}, ~c1); }
expr(e) ::= L_PAREN expr(e1) R_PAREN.             { e = e1; }

expr(e) ::= varref(e1).                           { e = e1; }
varref(e) ::= IDENT(lit).                         { e = _("varref", {_(lit)}, ~lit); }

expr(e) ::= fncall(e1).                           { e = e1; }
fncall(e) ::= FNCALL(lit1) arg_list(c2) R_PAREN.  { e = _("fncall", {_(lit1), c2}, ~lit1); }

arg_list(L) ::= .                                 { L = _("arglist"); }
arg_list(L) ::= expr(c1).                         { L = _("arglist", {c1}, ~c1); }
arg_list(L) ::= arg_list(L1) COMMA expr(e).       { L = L1 += e; }

expr(e) ::= FLOAT_LIT(lit).                       { e = _(lit); }
expr(e) ::= INT_LIT(lit).                         { e = _(lit); }

expr(e) ::= CHAR(lit).                            { e = _(lit); }
expr(e) ::= STRING(lit).                          { e = _(lit); }
```

Which is compiled and installed like: 

```
$ lempy_build expressions.lemon
````

Which can be used from Python like:
```
import expr_parse
import json
expression = expr_parse.parse('(5 + 7.2) / log(-24) + "nonsense"')
print(json.dumps(expression.as_dict(), indent=1))
with open('out.dot', 'w') as f:
     f.write(expr_parse.dotify(expression))
```

Ignoring the very verbose JSON output, after rendering with `$ dot
-Tpng -oexample_tree.png out.dot`, this yields:

![Example parse tree](example_tree.png)


# Prereqs


While the generated parser modules have no dependencies, this project
_itself_ has several.

Python 3.6+ required.

Only unix-like operating systems are supported right now. All the
important code is platform-agnostic, but the binary-building steps
assume a POSIX system C/C++ compiler that's command-line compatible
with `gcc`.

lemon-py depends on the C++ compiler supporting at least c++17. It
also depends on the `pybind11` PyPi module (not just headers installed
to system include paths), and probably the `python3-dev` system
package to get the `Python.h` header. Only standard C/C++ headers are
used otherwise.

On Ubuntu you can probably get everything you need from:

```
$ sudo apt install build-essential python3-dev
```


# Installing

If you downloaded the source from github, you can install via 
`python3 -m build` and `pip3 install`, or just add `src` to your 
`PYTHONPATH`. You can also download an installable Python package
from the releases page on github. Install that like 
`pip3 install ./lemon-py.tar.gz`.

I'm working on getting it on pypi, but my environment doesn't make
`twine` happy for some reason. It's also not cross-platform, and I
can't figure out how to red-list the right operating systems.


# Invocation

The following modules can be invoked from the command line directly by
using `python3 -m MOD ARGS...`. If you've installed the packaged
release, you'll also have access to short-form scripts that call these
modules. All of these modules support a `--help` argument that will
give usage information on the particular command and its options.

* [`lemon_py.BuildGrammar`] : `$ lempy_build [OPTIONS] path/to/grammar.lemon` -
  build a grammar file containing lemon-py lexer definitions and Lemon
  grammar into a loadable python module, and (optionally/usually)
  install that to the current user's local python packages.

  * `--terminals` - runs lemon on the input file and prints out a
    skeleton lexer definition.

  * `--debug` - build and install as normal, except instead of
    building intermediate files into a temporary directory, drop the
    files into the current working directory.

  * `--nobuild` - put the entire Python module source together, but
    don't call the compiler on it. Best used with `--debug`.

  * `--noinstall` - build the grammar, but skip the step where it's
    installed to the user's path. Best used with `--debug`.

  * `--unicode` - enable partial support for input languages needing
    unicode-aware regex. (See the section at the end of this readme
    for a better idea of what this does.)

  * `--cpp DIR` - instead of building a Python parser module, output
    an implementation file and clean header into the indicated
    directory.

* [`lemon_py.Driver`] : `$ lempy [OPTIONS] parser_module path/to/input.txt` -
  load a lemon-py parser module (not a raw grammar file), parse a file
  into a tree (accepting the input or throwing error), and optionally
  perform basic operations like graphical tree visualization and JSON
  export.

  * `--json` - dump the parse tree to the console in JSON format. Note
    that this can easily be an _enormous_ amount of text, as parse
    trees are highly redundant.

  * `--dot output_file` - dump a GraphViz `dot` representation of the
    parse tree into the given file. You can then render it with the
    `dot` binary.

  * `--vis` - if both `dot` and ImageMagick `display` are installed,
    this command will automatically display the `dot`-rendered parse
    tree.

Build a grammar into a loadable module and place it in your personal
site-packages path using the following command:

```
# long form
$ python3 -m lemon_py.BuildGrammar path/to/grammar.lemon

# shorter version (if you installed the package or `bin/` is in path)
$ lempy_build path/to/grammar.lemon
```

You can then load it in Python with `import MOD_NAME`, where
`MOD_NAME` is whatever you configured with the `@pymod` directive
inside the grammar file.


## Setting C/C++ Compiler

lemon-py internally uses your system's C compiler to bootstrap the
`lemon` executable, and then the C++ compiler each time you rebuild a
target grammar. 

At startup lemon-py will first check for the `CC` and `CXX`
environment variables, to set the C and C++ compilers respectively.

If not set via environment, lemon-py uses `clang` on MacOS and
`gcc`/`g++` everywhere else.


_____________________________________________________________________


# Parser Module API

Once you've built your language module, you can simply `import` it by
the name you gave it with the `@pymod` directive.

The module exports several free functions:

* `parse(input: str) -> ParseNode` - parses a string into a parse
  tree, returning the root node. Lex and parse errors generate
  `RuntimeError` with text describing the error and location.

Note that a common mistake when starting a new language is forgetting
to define the lexer in its entirety, covering all legal characters
that _might_ occur in a valid parse input all the way up to the end of
input--potentially using skips to pass over irrelevant characters like
whitespace and newlines. If your lexer is incomplete or incorrect, you
will always get lexer errors from `parse()`. Use `lempy_build
--terminals` to export a skeleton `@lexdef` block to make sure you
cover all terminals; this includes a default whitespace skip.

* `dotify(input: ParseNode) -> str` - returns a string representing
  the parse tree and its values, suitable for rendering using GraphViz
  `dot`. Note that this function does not call, link to, or depend on
  GraphViz or any other external library, doing all its processing as
  raw text. GraphViz is only needed to interpret the output.

The parse tree is represented by an extension class named
`ParseNode`. This class is implemented separately by each generated
parser module, and the functions above are only meant to work on
`ParseNode` instances created by `parse()` in the same module.

Overall, this object is not a good candidate for first-class data type
in a Python application: the object is a "value", more similar to a
single huge JSON document than a Pythonic expectation of references to
subobjects. Transforms such as reordering children, pivoting subtrees,
and other such operations are unsupported and undefined.

For non-trivial usage, it's suggested that the Python application
manipulate the parse tree only temporaily, usually to construct an
application-specific representation of the parsed structures. The
parse tree can then either be discarded, or maintained in memory to
aid in diagnostics. Keep in mind that parse trees may be enormous,
several times bigger than an application-specific abstract syntax tree
for the same structures.

`ParseNode` instances have the following read-only members:

* `.production: str` - the user-defined production/nonterminal name,
  or `None` if the node represents a terminal/token.

* `.type: str` - the Lemon-defined token name, or `None` if the node
  represents a nonterminal.

* `.value: str` - the lexer-extracted string value for a value
  token. For literal tokens, the original lexer input string. `None`
  for nonterminals.

* `.c: Sequence[ParseNode]` - the children of this node. In a typical,
  well-formed parse tree, all leaves are terminals and all internal
  nodes are nonterminals. Nothing in Lemon or lemon-py enforces this,
  however, and grammar actions are free to add children to nodes
  representing terminals.

* `.line: int` - the line number assigned to this node from the
  user-defined grammar actions. `-1` if undefined/unknown. Only
  terminals are automatically annotated by the lexer with the
  approximate line number, so parse nodes created directly from
  `Token` values will automatically have a line number attached. Parse
  nodes created with a nonterminal name must have their line number
  set within the grammar action; this does not happen automatically.

* `.id: int` - an identifier for this node guaranteed to be unique
  within a single tree. These are assigned in pre-order.

`ParseNode` also has the following mutable members:

* `.attr: dict` - not used by lemon-py, this is meant for applications
  to attach additional data to the parse tree. It may be used as you
  see fit; but it's generally recommended to use the parse tree only
  briefly to build a higher-level syntax tree.  `.attr` is geared more
  toward the temporary data necessary for those transforms than for
  rigorous in-tree analysis.

`ParseNode` has the following methods:

* `.as_dict() -> dict` - make a deep copy of this node and all
  children, recursively, returning the results as a regular Python
  `dict`. This is very useful for trivially interoperating with
  existing Python code designed for tree-of-`dict` data
  structures. This can enable a JSON parse-tree export with 1 line of
  code (see example above).

`ParseNode` supports the Python container interface over its children,
including iteration and subscripting. `mynode[2]` will return the 3rd
child of `mynode`. `for c in mynode` will iterate the children of
`mynode`.

`ParseNode` equality check (via `==`) is by-value and deep. Equality
checks that production, type, and value are equal (by string); and
that this is true for all children, recursively. This seems the most
useful check for equality within the value-tree framework, where we
typically want to know if the parse nodes express
syntactically-equivalent (sub)expressions.


# Module Definition

Lemon-py grammar definitions must include a `@pymod MODULE_NAME`
directive somewhere indicating the Python `MODULE_NAME` to
generate. This text is not removed before processing, and so should be
inside a comment. Example:

* `//@pymod parse_my_lang`


# Lexer Definitions

An integrated lexer is one of lemon-py's labor-saving
features. Without it, you would need to write a lexer for each target
language. For the purposes of this (and only this) section, words like
"returned" and "value" should be taken in the context of the internal
lexer->parser interface used inside of grammar production rules.

Lexer definitions appear inside a comment block as a run of
definitions, one per line, starting on the line following `@lexdef`
and continuing until `@endlex`. Note that these definition lines are
_not_ removed from the grammar input to `lemon`, and so should always
be inside a comment block. It's an error to define multiple `@lexdef`
blocks, and only the first one will be used.

Use `lempy_build --terminals` to output a skeleton lexer
definition. This is derived from Lemon's analysis of the grammar
definition, and is certain to include all terminals. You will want to
replace the defined literal values with your own language's token
literals, adjust terminator values and skips, and define value-type
tokens as described below.

The lexer itself is relatively basic. It handles two classes of tokens
("literal" and "value"), plus user-defined skip patterns and canonical
string support. Declarations of different classes may be mixed in the
`@lexdef` block, and the ordering rules discussed below apply only to
the relative definition of different tokens of the _same class_.

Note that whitespace is important as a delimeter in lexer definitions
themselves (separating elements of the definition line itself), but
leading and trailing whitespace is stripped from all definitions. It
is not possible to directly write a literal whitespace character to
match, nor is it possible to directly write a literal null character
to match.


## Regular Expressions

Regular expressions occur in many places within the lexer definition.
These are implemented using C++'s `std::regex`, set to the
`ECMAScript` dialect. Check a C++ reference for the precise regex
syntax supported.

Regular expressions within lexer definitions may by either case
sensitive, or case-insensitive. If the regular expression is
introduced with `::` it is case sensitive, and if introduced with `:`
it is case insensitive.  All places that regular expressions occur may
be defined as either case sensitive or not. Where you see `:
regular_expression` below, you could also write `::
regular_expression` to match with case sensitivity.

Leading and trailing whitespace is stripped from regular expressions,
but internal whitespace is preserved. No extra escapes are required:
e.g. for the whitespace class, write `\s`, **not** `\\s`.

WARNING: regular expressions that match the null (empty) string will
cause the lexer to enter an infinite loop. In the best case this looks
like repeatedly emitting the same useless token until there's a parse
error that throws an exception. But, especially for skip sequences,
this can simply result in an infinite loop internal to the lexer's
`next()` routine.


## Literals

"Literal" tokens are defined by a fixed string of
case-sensitive/exactly-matched characters, and are stored/matched with
a basic prefix tree. Literals are matched greedily, with the longest
matching sequence having priority. Thus the order in which you define
overlapping literals is largely irrelevant (although definition order
will influence search order between disjoint literals). Literal tokens
are returned by Lemon-defined code number, with a value equal to the
original lexer configuration string.

NOTE: leading/trailing whitespace are stripped from the string
literal, but internal whitespace is preserved.

Literal syntax: 
```
TOKEN := literal_string
```

Literal tokens have a higher priority than value tokens below, and so
may erroneously consume the prefix of a value token. For instance,
defining a literal matching `else` would consume the first part of a
variable named `elsewhere`, generating a mis-parse or parse error. On
the other hand, reprioritizing values higher than literals would
result in keyword `else` lexing as an identifier in the above example.

To solve this problem, literals may optionally be declared with a
terminator pattern. The pattern is a regular expression which _must
match_ the input after the literal string in order for the literal to
match. The terminator characters themselves are _not consumed_ as
input, but are only used as a lookahead check to aid in matching the
literal.

NOTE: Due to how these lines are parsed, you cannot define a literal
string that contains a `:` surrounded by white spaces. The right-hand
side of that string will be treated as a terminator.

Literal syntax with terminator: 
```
TOKEN := literal_string : regular_expression
```

## Values
 
"Value" tokens are defined by a regular expression, and are returned
with both a token code and a text value. By default the entire pattern
match is returned as the value. A single sub-match may be specified,
and if present the lexer will extract the sub-match as the token's
value--the input matching the entire pattern is still consumed from
the input stream. No type conversions are applied, all values are
returned as strings.

Value token patterns are checked in the same order they're specified
in the `@lexdef` block. Typically this means you should specify
longer, more specific patterns first; and then shorter, or more
generic patterns after. For instance, to keep your `INT_LITERAL` from
always eating the first part of a `FLOAT_LITERAL` (resulting in
subsequent lex or parse errors), define the floating-point literal
(with mandatory decimal point) _before_ the integer literal.

Value syntax: 
```
TOKEN : regular_expression
```

## Skips

Skip patterns are simply regexes that are used to skip whitespace,
comments, or other lexically and syntactically-irrelevant
content. Skip patterns are applied before every attempt at token
extraction. The `reminder_name` below is not used internally, and can
simply be used to label the skip pattern for human comprehension.

Skip syntax: 
```
!reminder_name : regular_expression
```

## Strings

Strings are handled by a configurable internal function that properly
handles escaping the delimiter and implements several common
string-lexing idioms. The delimiter, escape character, and behavior
are configurable for each string type you define.

A string lexer definition is declared by starting the line with the
`'` (single-quote) character. The next two characters are the
mandatory delimiter character and escape character,
respectively. Followed by optional control characters for special
string behavior. Finally, the token type for the string is declared
after the `:=`.

String syntax: 
```
' DELIM ESCAPE [SPECIALS] := TOKEN
```

The following special characters are defined:

* `s` or `!` - this type of string should span newlines. If **not**
  set, the lexer automatically considers it an error for a string to
  cross a newline boundary. This is usually desirable as it makes
  error reporting much easier when the user forgets to close a
  string. Set this character to disable the check and allow these
  strings to freely pass over newlines.

* `j` - join together adjacent occurrences of this string type. If
  multiple strings of this type occur in a row, separated only by
  skips, then all the strings will be joined together (without
  delimiters) as if they were all declared within a single set of
  delimeters.

All whitespace in the delimiter definition is ignored, including
between the delimiter and escape characters.

String values returned by the lexer _do not_ include the surrounding
delimiters.

Notes: Due to lazy lexdef parsing, you can't define a string with a
colon as a delimiter or escape.

String syntax examples:

Assign double-quote delimeted strings to `STR_TOK`, use the backslash
as an escape character, and join adjacent strings.  (This is mostly
like normal C-style strings.)

```
' "\j := STR_TOK
```

Assign at-sign delimited strings to `DOC`, use the semi-colon as an
escape character, and freely pass over the newline
character.

```
' @;s := DOC
```

... which lexes strings that look like this:


```
@look ;@ me 
over;;here@
```

## Token priority

Token classes are tested in the following order:

0. (skips)
1. EOF
2. strings
3. literals
4. values

Skips are applied repeatedly, before checking for the next lexical
token, until no skip consumes input. EOF is automatically emitted when
the lexer reaches end of input. If the lexer has not reached the end
of input, and no new token can be matched, an exception is
thrown. Calling the lexer after it has emitted EOF will simply return
`std::nullopt` indefinitely.

An example `@lexdef` can be found in the example at the top of this
readme.


# Parser Definition


The original Lemon parser-definition language is unchanged. Instead, a
standard set of headers is included providing definitions available
for use inside the parser actions. These definitions make it
relatively trivial to generate generic parse trees to represent
anything Lemon can parse.

**You may ignore memory management for nodes created using the
interfaces described in this section**. All parse resources are
tracked by the internal parser state, and anything that goes astray
will be automatically freed when the parser completes or fails. This
is one of the motivating features for this project, so please abuse it
merily and report leaking `ParseNode` on github.

You're on your own to manage memory for your own objects you create in
grammar actions. lemon-py defines a `%default_destructor` for the
Lemon grammar, but due to the tracked node pointers discussed above,
these are ultimately unnecessary for normal-sized inputs, serving only
to reduce interim memory usage and not to avoid memory leaks.  As a
result, you may use the occasional per-rule `%destructor` if you wish.

## Micro-DSL Syntax

Only a few, simple operations are necessary to build parse trees from
inside Lemon's parse actions. lemon-py makes these operations as terse
and easy as possible by defining a micro DSL using C++ operator
overloads. If you only want to make lemon-py parse trees, you do not
need to learn any C++ to write your grammar actions; merely learn the
syntax described here.

### Rules, Actions, and Metavars

An example Lemon production definition:

```
expr(e) ::= expr(c1) ADD(o) expr(c2).    { e = _("+", {c1, c2}, ~o); }
```

The line above defines a grammar rule (preceding the `.`) with
nonterminals in lower-case and terminals in upper case. [Note: this
case requirement applies only to the elements of the grammar rule, as
Lemon uses this distinction to distinguish between terminal and
non-terminal rules.]

The second half of the line, in curly brackets, defines a grammar
action. It is the grammar action that we use to actually build the
parse tree; without grammar actions, a lemon-py parser will always
report failure, even if the input string matches the grammar. Each
grammar action is called only when its rule matches the input, so
there is no need to check any additional status.

In addition to defining the production rule for an algebraic addition
expression, the line also defines "metavariables" (metavars) in
parentheses after each of the elements of the production so that we
can refer to the C objects within the action that represent the
grammatical elements in the production. It's not required to define a
metavar for each element, but it _is_ required to define a metavar for
each element you wish to refer to. For instance, this production could
omit the `(o)`, and instead draw line number information from the `c1`
subexpression instead. You may name the metavars whatever you wish, in
whatever case, so long as it is a valid C identifier--except `_`. You
may assign metavars to both nonterminals and teminals.

### Magic Variable `_` (underscore)

lemon-py causes every grammar action automatically to include the
special variable `_` (underscore). This is a handle for the parser state, 
and is used to interact with the lemon-py parse tree system. 
Leaving aside the interior details of how it's implemented, 
`_` can be used several ways from within the grammar action.

In the line above, `_` serves its most common purpose: creating a new
parse node. When used in this way, `_` acts like one of the following
functions:

```
_(production: str, children = {}, line = -1) -> node - create a new nonterminal node.
```

* returns a newly-created non-terminal parse node.

* `production` is a string name used for this node, often (but not
  always) derived from the actual grammar production left-hand side.

* `children` is an optional list of parse nodes to add as children to
  the newly-created node, enclosed in curly brackets.

* `line` is an optional line number for this node. If you do not
  include a line number, it will be recorded as `-1`. Use the special 
  operator `~` on any token or parse node to fetch its line number.
  Line numbers are _automatically_ extracted from TOKEN values, so when 
  promoting a token value to a terminal parse node this is not necessary. 
  For interior (production-named) nodes, you will need to source the line
  number from one of the tokens or child productions of the rule (as
  in the example above with the `o` token).

There is a second variant of the `_` function:

```
_(token: Token) -> node - promote a lexer token to a parse node.
```

This variant promotes a terminal/token value into a parse node. Since
line number is known for tokens (from the lexer), and terminals should
not have children, no other arguments are required. 

```
expr(e) ::= INT_LIT(lit).                         { e = _(lit); }
```

Note that `e = lit;` will not work, as productions and terminals do not
share the same C type.

### Parse Nodes

Parse nodes created inside actions and from metavars on the right-hand
side of the grammar production have several operations defined on
them: 

* First, they may be freely assigned to the metavar appearing on the
  left-hand side of the production rule. This is, in fact, _required_
  in order to link the structures together through the parser's
  recursion. Every useful action will consist of an assignment to the
  left-hand metavar.

* Second, they define a `node += node` operator that assigns the
  right-hand node as a child of the left-hand node, then returns the
  left-hand node.

* Third, they define a prefix unary `~` (tilde) operator that reads
  the stored line number of an _existing_ node. Note that it doesn't
  "figure out" the line, it just copies through whatever was set in
  the node's constructor. This operator also works on tokens, which
  have a line number automatically set by the lexer.

* Less frequently used is a subscript operator `node[{list, of,
  children}]` that appends all of the listed nodes as children of
  `node`, returning `node` (note the curly-brackets around the list
  inside the square brackets).

A canonical left-recursive list might look something like this:

```
arg_list(L) ::= .                                 { L = _("arglist"); }
arg_list(L) ::= expr(c1).                         { L = _("arglist", {c1}, ~c1); }
arg_list(L) ::= arg_list(L1) COMMA expr(e).       { L = L1 += e; }
```

Note that attempting to read anything from the metavar node on the
left-hand side of the production is (probably) a native memory access
error. Don't do it.


### Setting the root node

`_` makes a final appearance when you want to set the root node,
typically in your toplevel production rule. Simply assign the parse
node you wish to serve as the root of your parse tree to the `_`
variable.

```
toplevel ::= expr(c1).                            { _ = c1; }
```

Check out the examples for a better idea of how to use this
stuff. It's not especially intuitive if you've never used a parser
generator before, and teaching you to use one is beyond the scope of
this readme. I suggest the Dragon Book (you might know the one), or
any of the various tutorials on using Bison/Yacc. The concepts are
highly transferable.

The `test_grammars/parasol` directory contains a draft of the grammar
for a fairly "complete" programming language, including a non-trivial
expression tree. This goes beyond the designed-to-parse examples
usually provided with parser generators. One real-world "wrinkle" to
note is the use of the lexer to detect function calls and "scope refs"
instead of the parser (by `ID`-pattern with trailing `(` and `[`
respectively), thereby avoiding ambiguities around function argument
lists inside expression trees.


# C++

Generated parser modules are designed to be usable from C++
applications (almost) as easily as Python. Use `lempy_build --cpp
OUTPUT_DIR` to generate C++ files for your parser. The directory (and
each parent) will be created if it does not exist.

`ParseNode.hpp` is the clean public interface to the parser. You
should be able to add that into your regular include path no
problem. The interface is roughly identical to the Python interface
described above, using standard C++17 types. 

In addition to the `parser::ParseNode` itself, this header exports the
`parse()` and `dotify()` functions for parsing and visualizing
trees.

None of the internal types or machinery for the parser is
exposed. Likewise, this file does not change with grammar changes
(only lemon-py upgrades), so you only need to copy it from the
generated files once.

Note that the first line of the output header defines
`LEMON_PY_SUPPRESS_PYTHON`, disabling inclusion of the `pybind11`
headers and generation of the Python module interface. If you want the
Python interface built as well, remove that line from the header, make
sure `pybind11` is in your include path, and building `_parser.cpp`
should generate basically identical object code to the regular
`lempy_build` Python build.

`_parser.cpp` is a self-contained implementation of the entire lexer,
parser, and other internal doodads. You'll need to add that to your
build's source files, updating it each time that you modify your
grammar. Note that while this lemon-py parser _generation_ framework
has some lazy system dependencies, the code *generated* by the
framework should be platform-independent (unless you enable unicode
support, which is dependent on `sizeof(wchar_t) == 4`).


# Deep Matter

## How Does It Work?


Most of the moving parts are in `ParserImpl.cpp`, which implements the
lexer, parse tree, and Python module interface. A small amount of code
(`void _init_lexer(){...}`) is generated to configure the lexer from
the input file, but this is merely inserted into the generated Lemon
input file itself.

A number of seemingly-weird decisions in the C++ widgetry are the
result of adapting to the Lemon grammar action
environment. Critically, Lemon puts the token type and the type of
every nonterminal rule into a single giant C `union`. This means they
must be trivial types, which eliminates the direct use of smart
pointers or containers to manage memory within grammar
actions. Instead, the lemon-py `_parser_impl::Parser` object
internally allocates nodes into `unique_ptr`s and returns a raw
pointer to the same object for use only within the Lemon grammar
actions. Value tokens are treated similarly, with an integer
terminal-code value member, but pointer and index into a table instead
of an internal `std::string`. When the `Parser` is destructed, all of
the node and string memory is reclaimed and all
`_parser_impl::ParseNode` and `Token` objects created by that `Parser`
become invalid.

The micro-DSL for the parser actions is implemented through operator
overload on trivial (in the technical sense) pointer wrappers. These
operations and associated conversion operator definitions are
contained entirely within the parser implementation file and do not
leak into other translation units.

Before returning the parse tree to the high-level interface, and
before it is destroyed (of course), the entire pointer-based internal
parse tree is dereferenced and copied into a simplified, value-typed
representation (`parser::ParseNode`). The intermediate, indirect tree
is deallocated and the returned tree is free of any complicated
ownership semantics.

The python aspect of lemon-py reads the input grammar file, builds the
lexer configuration, and outputs a Lemon grammar file that inlines the
lexer configuration and includes the `ParserImpl.cpp` file to
implement the lexer and parser environment. The file is placed in a
temporary working directory and `lemon` is invoked on it to generate
the final source code for the parser module. The module is then built
with standard pybind11 settings, using `g++`. Finally, it is copied
into the current `python3` interpreter's personal site-packages for
the current user.


## Limitations


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
highly-redundant strings. For very large inputs, and especially for
deeply-nested non-terminal productions (complicated grammars), the
parse tree itself can be very large. In practice, for most industry
DSLs, hobby languages, and research languages, this is not a real
limitation. Inputs are rarely 1 MiB of text, let alone the GiB it
would take to challenge most modern computers.

These issues can be alleviated by re-using the proven Lemon grammar
with a custom (buffering) lexer, grammar actions, and/or tree type
when moving from typical usage to large-input usage. The biggest gains
come from rewriting with a high-level AST, built directly in the
grammar actions, which can collapse many intermediate parse nodes into
fewer syntax nodes.

Finally, in the memory hog department, both Lemon and I use recursive
functions all over the place. If you have really recursive grammars or
extremely long lexer literals, you might run into the stack limit on
your machine. I've been programming for 25+ years now have have _only_
run out of stack when there's been an error causing infinite
recursion.


## Unicode and Binary Support


[Warning: the options in this section generate parser code that is
**not** platform-agnostic. Default ASCII parsing should be
platform-independent.]

lemon-py parsers have _partial_, **experimental** support for UTF-8
input languages on GCC and Clang platforms where `sizeof(wchar_t) ==
4`. `std::u32string` support is apparently not sufficient, as
`wchar_t` and `uchar32_t` are disjoint types even when they are the
same size.

To enable unicode support, compile your grammar with the `--unicode`
flag. This has two basic effects:

* lemon-py will use `wchar_t` and `wstring` internally to represent
  all strings during the parsing process.

* lemon-py will assume the input is UTF-8 and _convert_ from UTF-8 to
  UTF-32 on input. It will do the opposite on output, encoding the
  entire parse tree into UTF-8 before returning it.

Literal and string support work as expected. Those code paths are
binary/utf-8 clean in 8-bit, so they're equally functional in 32-bit.

Regex support is where things are lacking. lemon-py uses the C++
`std::regex` package for all of its regular expressions. Regex support
is only required, per the standard, for `char` and `wchar_t`. The
`char` version is not UTF-8 aware, and so _in the regex definition
itself_, it interprets characters with multiple codepoints as separate
bytes. For sequences that works fine, but a pattern like `[тес]` does
not match on one the visible characters, but any one of the 6 bytes
that make up the utf-8 string "тес".

Enabling `--unicode` on platforms where `wchar_t` is 4-bytes long
_appears_ to make regex work properly with the non-ASCII European
character sets I've tried. But features are limited. Many character
classes are incomplete or incorrect, case conversion is undefined for
most languages (meaning all unicode matches effectively behave as
case-sensitive regardless of settings), you can easily get bad lexes
with mixed kinds of whitespace, etc. Speakers of languages with
disputes about their local alphabet order will need to consult a
unicode reference.

What almost certainly does not work is any unicode character that
_still_ won't fit in a 4-byte word.

You should only enable unicode support when you're actually parsing
unicode, as just by default absolutely every string and character in
the parser consumes 4x memory. It also introduces a number of copy
operations when it needs to convert between UTF-8 and UTF-32
internally, which can slow things down. Finally, it adds to the C++
compile time for the finished parser module.

Notes:

* You do not need unicode support for your grammar to include its own
  unicode string terminals. The canonical string support will catch
  any ASCII-delimeter cleanly, and return the correct string even if
  there are utf-8 characters in between.

* "Real" unicode support, like integrating ICU, is entirely beyond the
  scope of this project. This has already grown well beyond the little
  "regex lexer + string tree" I had in mind when I started, and trying
  to integreate compliant, normalizing unicode support into the
  lemon-py "invisible" build-chain sounds like no fun at all.

In standard 8-bit/ASCII mode, the lexer/parser should be binary
clean. Again, regex are going to be the pinch point, as you'll need to
encode all your patterns using the `\x` operator to set hex codes.


## Motivation and Alternatives

I want to play with definitions and implementations of programming
languages. It's hard enough to invent an unambiguous, expressive
syntax without then needing to reconfigure it to avoid parser
generator limitations. Since language definitions are often deeply
recursive, you can spend quite a lot of work before you discover some
parse ambiguity that necessitates a restructuring of a large part of
your definition. This especially crops up in the deeply-recursive
aspects of programming languages like expression syntaxes (`5 + max(2,
some_func() - 0.2) / 6.2`), at which point controlling operator and
production precedence becomes critical.

Literally every time I have tried an LL or PEG parser generator on any
sufficiently-complicated grammar, I have run into some ridiculous
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
the Lemon parser code implements consistent memory reclamation and is
safe by default for multiple instantiation with no state shared
between instances. I also prefer Lemon's overall syntax to `bison`. In
Lemon, I can simply write down the grammar for my language and resolve
ambiguities abstractly instead of by coercing the grammar itself.

At the same time, Lemon is a fairly old project (started in the 80's)
and has a relatively naive API. While it's suitable for directly
generating an AST with appropriate types and grammar rules, it works
only with "trivial" C types, provides no default/generic parse tree
data, and it requires an external, proper lexer. As a result, when
working with it in the past, I have been inclined to develop the
target language's custom lexer and AST type framework at the same time
as the parser grammar. Since Lemon works only with raw pointers, this
inclines the AST design toward owning raw pointers, which feels pretty
gross here in 2021.

Likewise, since I'm rarely targeting general-purpose languages where
translation units become enormous, nor am I often targeting machine-
code back ends where bit-twiddling and isomorphic binary
representation are routine, nor do I need blinding performance...
implementing the whole of my compiler in C++ from the very start feels
onerous. I want my favorite parser available from Python, with the
option of using the same grammar in C++ if I'm ready to build a native
compiler.

There exist other LR or LALR parser generators for Python that I know
of:

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
that better than writing C++ grammar actions, though.

Lark is probably the closest in capabilities and design to lemon-py,
in that it is driven by a data file (string) defining the grammar. It
also has a built-in lexer. However, its implementation of LALR(1)+EBNF
does not support operator associativity or precedence, meaning that
despite the algorithm it uses, it functionally has many of the same
shortcomings as typical LL and PEG parser generators.

Of those options, only `pyleri` is capable of outputting a parser in
C. I found it only after I started this project, and its syntax left
me undeterred from finishing.


# Anticipated Questions

* Windows support?

  * I have no idea how to easily, automatically compile a Python
    extension on Windows. I think we would need to output a VS
    project, and that's more than I want to deal with. Especially
    because I haven't written code on Windows in years. If you know
    how to do this, please submit a PR.

    Also, parsers built for windows would be limited to ASCII parsing,
    as Windows defines `sizeof(wchar_t) == 2`.


* OSX support?

  * Probably-maybe it works? I haven't tested it. I don't have a
    Mac. It really comes down to whether or not lemon-py can find a
    compiler.


* Why C++ instead of C?

  * I like modern C++. I don't really like solving the same problems
    in boilerplate C.


* Why C++17 instead of C++(`n < 17`)?

  * I like modern C++. I don't really like solving the same problems
    in antique C++.


* Why C++17 instead of C++(`n > 17`)?

  * System compiler support.


* Why doesn't lemon-py automatically write grammar actions?

  * That sounds like a lot of work with relatively minimal gains.

    The operator-overload DSL provided for use within grammar actions
    is as terse as permitted by C++ and my cleverness while still
    providing full flexibility in parse tree structure. An automatic
    action-generation system would either impose unreasonably
    inflexible requirements on production structure (such as
    rigorously conventional naming). Or it would require that
    child-order and ellision information currently passed via function
    call argument order and assignment operations is instead defined
    via some encoding of metavariable names. Parsing all that out
    sounds boring, and I remain entirely unconvinced that the result
    would be either more readable or more maintainable than the
    current approach.


* Doesn't the use of `_` as a variable name violate some standard 
or shadow some standard definition or something?

  * Nope. As far as I can tell, it's reserved at global scope, but not
    at function scope.

    Historically, this project started out using `p` as the magic
    variable.  But as I polished the interface and documentation, it
    became apparent that people might like to use `p` as a metavar
    much more than as the magic constructor. At this point, the only
    improvement I'm planning is automatic conversion of token to parse
    node types, and only if I can figure out how _not_ to pass a
    `Parser*` to every `Token`.



