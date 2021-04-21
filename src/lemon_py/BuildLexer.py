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
import collections

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


def escape_backslash(s: str, extra: str = '"'):
    return s.replace('\\', '\\\\').replace(extra, '\\' + extra)


def scan_regex(s: str): # input should _not_ be stripped!
    if not s or len(s) == 1: return None    # this is an empty string or a ':' by itself.
    matchtype = s[0]
    flags = 'RegexScannerFlags::Default'
    if matchtype == ':':
        flags = 'RegexScannerFlags::CaseSensitive'
        s = s[1:]
    return (escape_backslash(s.strip()), flags)


def scan_literal(s: str): # input should _not_ be stripped!
    if not s or len(s) == 1: return None   # this is an empty string or just a '=' by itself
    
    terminal_pattern_start = re.search(INTRO_REGEX_REGEX, s)
    
    if not terminal_pattern_start:  # just take everything as the search string
        return (escape_backslash(s[1:].strip()), None)
    
    stringlit = s[1:terminal_pattern_start.span(1)[0]].strip()
    relit = s[terminal_pattern_start.span(1)[1]:]

    terminator = scan_regex(relit)
    
    return (escape_backslash(stringlit), terminator)

def scan_lex_line(l: str):
    l = l.strip()
    if not l: return None

    if l[0] == '!': # skip pattern marker
        halves = re.split('\s+:', l, 1)
        return ('skip', halves[0][1:].strip(), scan_regex(halves[1]))
    elif l[0] == '\'': # stringdef marker
        halves = l.rsplit(':=', 1)
        return ('string', halves[1].strip(), halves[0][1:].strip())
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


def scan_lexer_def(lemon_source: str):
    start = lemon_source.find('@lexdef')
    if start < 0:
        raise RuntimeError("No lexer definition found.")
    end = lemon_source.find('@endlex', start)
    rawlines = lemon_source[start:end].splitlines()[1:]
    scanned_lines = list(filter(lambda ld: ld, map(scan_lex_line, rawlines)))
    return scanned_lines

def decode_stringdef(tokname, code):
    code = re.sub('\s+', '', code)  # get rid of whitespace in the code
    delim = escape_backslash(code[0], '\'')
    escape = escape_backslash(code[1], '\'')
    special = code[2:]

    flags = "StringScannerFlags::Default"

    if '!' in special:
        flags += " | StringScannerFlags::SpanNewlines"
    
    if '<' in special:
        flags += " | StringScannerFlags::JoinAdjacent"

    return f"Lexer::add_string_def('{delim}', '{escape}', {tokname}, {flags});\n"

def implement_lexdef_line(lexdef: tuple) -> str:
    retval = '' + TABBY
    kind = lexdef[0]
    tokname = lexdef[1]
    if kind == 'skip':
        skipre = lexdef[2]
        retval += f"Lexer::add_skip(\"{skipre[0]}\", {skipre[1]});\n"
    elif kind == 'value':
        retval += f"Lexer::add_value_type({tokname}, \"{lexdef[2]}\", {lexdef[3]});\n"
    elif kind == 'literal':
        if lexdef[3]:
            termre = lexdef[3]
            retval += f"Lexer::add_literal({tokname}, \"{lexdef[2]}\", \"{termre[0]}\", {termre[1]});\n"
        else:
            retval += f"Lexer::add_literal({tokname}, \"{lexdef[2]}\");\n"
    elif kind == 'string':
        retval += decode_stringdef(lexdef[1], lexdef[2])
    
    if kind not in ('skip'):
        retval += TABBY + f"token_name_map.emplace({tokname}, \"{tokname}\");\n"

    return retval


def extract_lexer_def(lemon_source: str) -> List[Tuple[str, str]]:
    start = lemon_source.find('@lexdef')
    if start < 0:
        raise RuntimeError("No lexer definition found.")
    end = lemon_source.find('@endlex', start)
    rawlines = lemon_source[start:end].splitlines()[1:]

    lines = map(lambda s: tuple(s.strip().split(':', 1)), rawlines)
    lines = filter(lambda t: len(t) == 2, lines)
    lines = list(map(lambda t: (t[0].strip(), t[1].strip()), lines))
    return lines


def implement_skip(re_str: str):
    return f"Lexer::add_skip(\"{escape_backslash(re_str)}\");\n"

def map_literal_token_and_value(token:str, value: str):
    return map_token_name(token) + f"      token_literal_value_map.emplace({token}, \"{escape_backslash(value)}\");\n"

def implement_literal(token, litval):
    if ":" in litval: # handle a literal with a terminator
        termsplit = re.split('\s+:\s+', litval)
        reallit = escape_backslash(termsplit[0].strip())
        if len(termsplit) == 2 and reallit and termsplit[1].strip(): # I should write a real parser for this...
            return f"Lexer::add_literal({token}, \"{reallit}\", \"{escape_backslash(termsplit[1].strip())}\");\n" + map_literal_token_and_value(token, reallit)
        
    return f"Lexer::add_literal({token}, \"{escape_backslash(litval)}\");\n" + map_literal_token_and_value(token, litval)


def implement_regex(token, re_str):
    return f"Lexer::add_value_type({token}, \"{escape_backslash(re_str)}\");\n" + map_token_name(token)


def implement_stringdef(delim_spam: str, token):
    # delim_spam looks like: '   "\ 
    # whole line looks like maybe: '  @ ^ ! :=  SOME_TOK
    delim_spam = re.sub('\s+', '', delim_spam[1:])
    delim = escape_backslash(delim_spam[0], "'")
    escape = escape_backslash(delim_spam[1], "'")
    shouldSpan = "true" if len(delim_spam) > 2 and delim_spam[2] == '!' else "false"
    impl = f"Lexer::add_string_def('{delim}', '{escape}', {token}, {shouldSpan});\n"
    return impl + map_token_name(token)


def map_token_name(token):
    return f"      token_name_map.emplace({token}, \"{token}\");\n"


def implement_lexer(lexer_def: List[Tuple[str, str]]) -> str:
    retval = LEXER_START[:]
    retval += TABBY + 'token_name_map.emplace(0, "EOF");\n'
    retval += TABBY + 'token_literal_value_map.emplace(0, "<lexer eof>");\n\n'
    for tok, pattern in lexer_def:
        if tok.startswith("'"):
            stok = pattern.strip()[1:].strip() # for strings, the token/pattern switch places
            retval += TABBY + implement_stringdef(tok, stok) 
        elif tok.startswith('!'):
            retval += TABBY + implement_skip(pattern)
        elif pattern.startswith('='):
            retval += TABBY + implement_literal(tok, pattern[1:].strip())
        else:
            retval += TABBY + implement_regex(tok, pattern)
        retval += "\n"

    retval += LEXER_END[:]

    return retval


def make_lexer(lemon_source: str) -> str:
    return LEXER_START + "\n".join(map(implement_lexdef_line, scan_lexer_def(lemon_source))) + LEXER_END