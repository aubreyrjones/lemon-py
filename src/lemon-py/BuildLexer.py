from typing import *

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

def extract_lexer_def(lemon_source: str) -> List[Tuple[str, str]]:
    start = lemon_source.find('/*LEXDEF')
    if start < 0:
        raise RuntimeError("No lexer definitions found.")
    end = lemon_source.find('*/', start)
    lines = list(map(lambda s: tuple(s.strip().split(':')), lemon_source[start:end].splitlines()[1:]))
    lines = list(map(lambda t: (t[0].strip(), t[1].strip()), lines))
    return lines

def implement_skip(regex: str):
    return f"_parser_impl::Lexer::add_skip(\"{regex}\");\n"

def implement_literal(token, litval):
    return f"_parser_impl::Lexer::add_literal({token}, \"{litval}\");\n"

def implement_regex(token, regex):
    return f"_parser_impl::Lexer::add_value_type({token}, \"{regex}\");\n"

def implement_lexer(lexer_def: List[Tuple[str, str]]) -> str:
    retval = LEXER_START[:]
    for tok, pattern in lexer_def:
        if tok.startswith('!'):
            retval += "      " + implement_skip(pattern)
        elif pattern.startswith('='):
            retval += "      " + implement_literal(tok, pattern[1:].strip())
        else:
            retval += "      " + implement_regex(tok, pattern)
    
    retval += LEXER_END[:]

    return retval


def make_lexer(lemon_source: str) -> str:
    return implement_lexer(extract_lexer_def(lemon_source))