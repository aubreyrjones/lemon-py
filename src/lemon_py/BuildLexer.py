# MIT License

# Copyright (c) 2021 Aubrey R Jones

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from typing import *
import re

LEXER_START = \
'''
namespace _parser_impl {
void _init_lexer() {
    static bool isInit = false;
    if (isInit) return;
    isInit = true;

'''
LEXER_END = \
'''
} 
} //namespace

'''

TABBY = "      "

INTRO_REGEX_REGEX = '\s+(:)[:]?\s+'


def escape_backslash(s: str, extra: str = '"') -> str:
    return s.replace('\\', '\\\\').replace(extra, '\\' + extra)


def scan_regex(s: str) -> tuple: # input should _not_ be stripped!
    if not s or len(s) == 1: return None    # this is an empty string or a ':' by itself.
    matchtype = s[0]

    if matchtype == '=':
        raise RuntimeError("Literal definition (:=) where regex (:/::) expected.")

    flags = 'RegexScannerFlags::Default'
    if matchtype == ':':
        flags = 'RegexScannerFlags::CaseSensitive'
        s = s[1:]
    return (escape_backslash(s.strip()), flags)


def scan_literal(s: str) -> tuple: # input should _not_ be stripped!
    if not s or len(s) == 1: return None   # this is an empty string or just a '=' by itself
    
    terminal_pattern_start = re.search(INTRO_REGEX_REGEX, s)
    
    if not terminal_pattern_start:  # just take everything as the search string
        return (escape_backslash(s[1:].strip()), None)
    
    stringlit = s[1:terminal_pattern_start.span(1)[0]].strip()
    relit = s[terminal_pattern_start.span(1)[1]:]

    terminator = scan_regex(relit)
    
    return (escape_backslash(stringlit), terminator)

def scan_lex_line(l: str) -> tuple:
    l = l.strip()
    if not l: return None

    if l[0] == '!': # skip pattern marker
        halves = re.split('\s+:', l, 1)
        return ('skip', halves[0][1:].strip(), scan_regex(halves[1]))
    elif l[0] == "'": # stringdef marker
        halves = l.rsplit(':=', 1)
        return ('string', halves[1].strip(), halves[0][1:].strip()) # reverse pattern/tok for string
    else:
        splitpoint = l.find(':') # find the first one (there could be a second in a literal)
        tokname = l[0:splitpoint].strip()
        if not tokname:
            return None
        matchtype = l[splitpoint + 1]
        if matchtype == '=':
            return ('literal', tokname, *scan_literal(l[splitpoint + 1:]))
            pass #literal
        else:
            return ('value', tokname, *scan_regex(l[splitpoint + 1:]))


def scan_lexer_def(lemon_source: str) -> List[tuple]:
    start = lemon_source.find('@lexdef')
    if start < 0:
        raise RuntimeError("No lexer definition found.")
    end = lemon_source.find('@endlex', start)
    rawlines = lemon_source[start:end].splitlines()[1:]
    scanned_lines = list(filter(lambda ld: ld, map(scan_lex_line, rawlines)))
    return scanned_lines


# Decode the little language for configuring strings.
def decode_stringdef(tokname, code) -> str:
    code = re.sub('\s+', '', code)  # get rid of whitespace in the code
    delim = escape_backslash(code[0], "'")  # escape single-quote because it's gonna be in a `char` not a `const char*`.
    escape = escape_backslash(code[1], "'")
    special = code[2:]

    flags = "StringScannerFlags::Default"

    if '!' in special or 's' in special:
        flags += " | StringScannerFlags::SpanNewlines"
    
    if 'j' in special:
        flags += " | StringScannerFlags::JoinAdjacent"

    return f"Lexer::add_string_def('{delim}', '{escape}', {tokname}, {flags});\n"

def cstring(s: str, uni: bool) -> str:
    if uni:
        return f'L"{s}"'
    else:
        return f'"{s}"'

def implement_lexdef_line(lexdef: tuple, uni: bool) -> str:
    cs = lambda s: cstring(s, uni)
    retval = '' + TABBY
    kind = lexdef[0]
    tokname = lexdef[1]
    if kind == 'skip':
        skipre = lexdef[2]
        retval += f"Lexer::add_skip({cs(skipre[0])}, {skipre[1]});\n"
    elif kind == 'value':
        retval += f"Lexer::add_value_type({tokname}, {cs(lexdef[2])}, {lexdef[3]});\n"
    elif kind == 'literal':
        if lexdef[3]:
            termre = lexdef[3]
            retval += f"Lexer::add_literal({tokname}, {cs(lexdef[2])}, {cs(termre[0])}, {termre[1]});\n"
        else:
            retval += f"Lexer::add_literal({tokname}, {cs(lexdef[2])});\n"
    elif kind == 'string':
        retval += decode_stringdef(lexdef[1], lexdef[2])
    
    if kind not in ('skip'):
        retval += TABBY + f"token_name_map.emplace({tokname}, {cs(tokname)});\n"

    return retval



def make_lexer(lemon_source: str, uni = False) -> str:
    return LEXER_START + "\n".join(map(lambda ld: implement_lexdef_line(ld, uni), scan_lexer_def(lemon_source))) + LEXER_END