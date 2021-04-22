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

import subprocess
import tempfile
import os.path
import os
import pybind11
import site
import shutil
import sys

from .BuildLexer import make_lexer

__all__ = ['build_lempy_grammar']

def _data_file(*filename: str):
    return os.path.join(os.path.abspath(os.path.dirname(__file__)), *filename)

_system_default_cc = 'gcc'
_system_default_cxx = 'g++'

# if sys.platform == 'win32':
#     _system_default_cc = 'cl'
#     _system_default_cxx = 'cl'

if sys.platform == 'darwin':
    _system_default_cc = 'clang'
    _system_default_cxx = 'clang++'    

c_COMPILER = _system_default_cc if 'CC' not in os.environ else os.environ['CC']
cpp_COMPILER = _system_default_cxx if 'CXX' not in os.environ else os.environ['CXX']

# this is the text injected to include the implementation
IMPL_TEXT = "\n#include <ParserImpl.cpp> // include the implementation file \n\n"

# for C++, we just rewrite everything into one giant file, so this can be null
CPP_IMPL_TEXT = "\n"


GRAMMAR_HEADER_FILE = _data_file("header.lemon")
LEMON = _data_file("lemon")
LEMON_TEMPLATE = _data_file("lempar.c")

def _bootstrap_lemon():
    '''
    Build the `lemon` executable if it doesn't already exist.
    '''
    if os.path.isfile(_data_file('lemon')):
        return
    
    print("Bootstrapping `lemon`.")
    command = [c_COMPILER, '-O2', '-o', _data_file('lemon'), _data_file('lemon.c')]
    subprocess.check_call(command)


def _read_all(filename: str):
    '''
    Open and read a whole file.
    '''
    with open(filename, 'r') as f:
        return f.read()


def _replace_token_defines(impl_text, defines) -> str:
    '''
    Replace the special sentinel struct with the token definitions.
    '''
    return impl_text.replace('struct _to_be_replaced{};\n', defines)


def _read_impl_and_replace_tokens():
    '''
    Read in the implementation file from the package dir, and the header
    from the current dir. Inline the token defs and return.
    '''
    impl_text = _read_all(_data_file("ParserImpl.cpp"))
    defines = _read_all('concat_grammar.h')  #this has to be in the cwd
    return _replace_token_defines(impl_text, defines)


def _copy_cpp_stuff(target_dir: str):
    '''
    Assuming the current dir has a complete, post `lemon` build,
    this will copy a build-ready header and implementation to the
    indicated director.
    '''
    if not os.path.isdir(target_dir):
        os.makedirs(target_dir)

    with open(os.path.join(target_dir, 'ParseNode.hpp'), 'w') as out:
        out.write('#define LEMON_PY_SUPPRESS_PYTHON 1\n\n')
        out.write(_read_all(_data_file("ParseNode.hpp")))
    
    parser_text = _read_all('concat_grammar.c')

    impl_text = _read_impl_and_replace_tokens()
    
    with open(os.path.join(target_dir, "_parser.cpp"), 'w') as outimpl:
        outimpl.write(impl_text + parser_text)


def _gpp_command(module_name: str):
    '''
    Create a command to build the parser in the current directory.
    '''
    pyinclude = subprocess.check_output(['python3-config', '--includes']).decode().strip()
    pylink = subprocess.check_output(['python3-config', '--ldflags']).decode().strip()

    retval = [cpp_COMPILER]
    retval.append('-g')
    retval.extend('-Wall -shared -std=c++17 -fPIC -fvisibility=hidden'.split())
    retval.extend(pyinclude.split())
    retval.extend(pylink.split())
    retval.extend([
#        "-Wl,-z,defs",
        f"-DPYTHON_PARSER_MODULE_NAME={module_name}",
        f"-I{pybind11.get_include()}",
        f"-I{os.path.dirname(__file__)}", # add this directory to pick up 'ParseNode.hpp'
        f"-I{os.path.abspath('.')}",
        "concat_grammar.c",
        "-o",
        f"{module_name}.so"
    ])
    return retval


def _concatenate_input(grammar_text: str, impl_text: str, uni: bool):
    '''
    Build the whole grammar input file for lemon. This has
    the real grammar text, the codegen'd lexer init, and
    the `header.lemon` contents.
    '''
    with open(GRAMMAR_HEADER_FILE, 'r') as f:
        header_text = f.read()

    lexer_def = make_lexer(grammar_text, uni)
    codegen_text = f"%include {{\n{impl_text + lexer_def}\n}}\n"

    whole_text = grammar_text + "\n\n" + codegen_text + header_text

    return whole_text


def _write_lemon_input(whole_text: str, grammar_module_name: str):
    '''
    Write the input and call lemon to process it.
    '''
    with open('concat_grammar.lemon', 'w') as f:
        f.write(whole_text)
    subprocess.call([LEMON, f"-T{LEMON_TEMPLATE}", "concat_grammar.lemon"]) # f"-d{str(workdir)}",


def write_and_build_curdir(whole_text: str, grammar_module_name: str):
    '''
    Output the augmented grammar, run `lemon`, and build the results with the C++ compiler.
    '''
    _write_lemon_input(whole_text, grammar_module_name)
    
    subprocess.call(_gpp_command(grammar_module_name))


def _extract_module(text: str):
    '''
    Get the @pymod name out of the text.
    '''
    BAD_NAME = "lemon_derived_parser_with_an_obnoxiously_long_name"  # that oughta lern 'em not to put a @pymod

    start = text.find('@pymod') # should look like maybe "//@pymod  \t foo_parser   "
    if start < 0:
        return BAD_NAME
    
    end = text.find("\n", start)

    linesplit = text[start:end].split()
    if len(linesplit) < 2:
        return BAD_NAME
    return linesplit[1].strip()


def _lexdef_skeleton(tokname: str):
    '''
    Output a trivial lexdef for the given token.
    '''
    justlen = 16 - len(tokname)
    return tokname + ":=".rjust(justlen) + " " + tokname.lower() + ":".rjust(justlen) + " [^\w_]"


def print_lang_header():
    '''
    Print out a list of all the token names that were defined by the grammar being built.
    '''
    with open('concat_grammar.h', 'r') as header_file:
        print("/*\n@pymod unnamed_language\n\n@lexdef\n\n!whitespace : \s+\n")
        lines = map(lambda l: _lexdef_skeleton(l.split()[1].strip()), header_file.readlines())

        print("\n".join(lines))
        print("@endlex\n*/")


def build_lempy_grammar(grammar_file_path: str, install = False, use_temp = True, print_terminals = False, cpp_dir = None, use_unicode = False):
    _bootstrap_lemon()

    grammar_file_path = os.path.abspath(grammar_file_path)
    print("Compiling: " + grammar_file_path)
    
    old_dir = os.path.abspath(os.curdir)

    grammar_text = _read_all(grammar_file_path)
    grammar_module_name = _extract_module(grammar_text)

    concatenated_text = _concatenate_input(grammar_text, CPP_IMPL_TEXT if cpp_dir else IMPL_TEXT, use_unicode)

    with tempfile.TemporaryDirectory() as workdir:
        if use_temp:
            os.chdir(workdir)

        if cpp_dir:
            _write_lemon_input(concatenated_text, grammar_module_name)
            _copy_cpp_stuff(cpp_dir)
        else:
            write_and_build_curdir(concatenated_text, grammar_module_name)
            if install:
                soname = f"{grammar_module_name}.so"
                shutil.copy2(soname, os.path.join(site.getusersitepackages(), soname))

        if print_terminals:
            print_lang_header()

        os.chdir(old_dir)    
    
    pass

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description="Build a grammar and optionally install it to the python path.")
    ap.add_argument('--unicode', default=False, const=True, action='store_const', help="Enable unicode support. This is necessary for reliable non-ASCII input, but increases memory usage in the resulting parser.")
    ap.add_argument('--cpp', type=str, required=False, help="Specify to output C++ compatible files to the indicated directory. Disables building the Python module.")
    ap.add_argument('--terminals', default=False, const=True, action='store_const', help="Print a skeleton `@lexdef` including all grammar-defined terminals.")
    ap.add_argument('--debug', default=False, const=True, action='store_const', help="Don't use a temp directory, dump everything in cwd.")
    ap.add_argument('--noinstall', default=False, const=True, action='store_const', help="Don't install the language, most useful with --debug.")
    ap.add_argument('grammar_file', type=str, help="The grammar file to build.")
    args = ap.parse_args()

    build_lempy_grammar(args.grammar_file, not (args.noinstall or args.cpp), not args.debug, args.terminals, os.path.abspath(args.cpp) if args.cpp else None, args.unicode)