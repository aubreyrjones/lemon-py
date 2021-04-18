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

def escape_backslash(s: str):
    return s.replace('\\', '\\\\')

def extract_lexer_def(lemon_source: str) -> List[Tuple[str, str]]:
    start = lemon_source.find('@lexdef')
    if start < 0:
        raise RuntimeError("No lexer definition found.")
    end = lemon_source.find('@endlex', start)
    lines = map(lambda s: tuple(s.strip().split(':', 1)), lemon_source[start:end].splitlines()[1:])
    lines = filter(lambda t: len(t) == 2, lines)
    lines = list(map(lambda t: (t[0].strip(), t[1].strip()), lines))
    return lines


def implement_skip(re_str: str):
    return f"_parser_impl::Lexer::add_skip(\"{escape_backslash(re_str)}\");\n"


def implement_literal(token, litval):
    if ":" in litval:
        termsplit = re.split('\s+:\s+', litval)
        if len(termsplit) == 2 and termsplit[0].strip() and termsplit[1].strip():
            return f"_parser_impl::Lexer::add_literal({token}, \"{escape_backslash(termsplit[0].strip())}\", \"{escape_backslash(termsplit[1].strip())}\");\n" + map_token_name(token)
        
    return f"_parser_impl::Lexer::add_literal({token}, \"{escape_backslash(litval)}\");\n" + map_token_name(token)


def implement_regex(token, re_str):
    return f"_parser_impl::Lexer::add_value_type({token}, \"{escape_backslash(re_str)}\");\n" + map_token_name(token)


def map_token_name(token):
    return f"      token_name_map.emplace({token}, \"{token}\");\n\n"


def implement_lexer(lexer_def: List[Tuple[str, str]]) -> str:
    retval = LEXER_START[:]
    retval += '      token_name_map.emplace(0, "EOF");\n\n'
    for tok, pattern in lexer_def:
        if tok.startswith('"'):
            stok = pattern.strip()[1:].strip()
            retval += f"      _parser_impl::Lexer::doubleQuoteToken = {stok};\n"
            retval += map_token_name(stok)
        elif tok.startswith("'"):
            stok = pattern.strip()[1:].strip()
            retval += f"      _parser_impl::Lexer::singleQuoteToken = {stok};\n"
            retval += map_token_name(stok)
        elif tok.startswith('!'):
            retval += "      " + implement_skip(pattern)
        elif pattern.startswith('='):
            retval += "      " + implement_literal(tok, pattern[1:].strip())
        else:
            retval += "      " + implement_regex(tok, pattern)
    
    retval += LEXER_END[:]

    return retval


def make_lexer(lemon_source: str) -> str:
    return implement_lexer(extract_lexer_def(lemon_source))