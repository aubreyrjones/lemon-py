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

    #impl_text = _read_impl_and_replace_tokens() # already done.
    
    with open(os.path.join(target_dir, "_parser.cpp"), 'w') as outimpl:
        outimpl.write(parser_text)


def _gpp_command(module_name: str):
    '''
    Create a command to build the parser in the current directory.
    '''
    pyinclude = subprocess.check_output(['python3-config', '--includes']).decode().strip()
    pylink = subprocess.check_output(['python3-config', '--ldflags']).decode().strip()

    retval = [cpp_COMPILER]
    retval.append('-O2')
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


def _concatenate_implementation(**kwargs):
    retval = ''

    static_impl_text = _read_impl_and_replace_tokens()

    if not kwargs.get('separate_interface', False):
        static_impl_text = static_impl_text.replace('#include <ParseNode.hpp>\n', _read_all(_data_file("ParseNode.hpp")))

    if kwargs.get('suppress_python', False):
        retval += '#define LEMON_PY_SUPPRESS_PYTHON 1\n\n'
    
    if kwargs.get('use_unicode', False):
        retval += '#define LEMON_PY_UNICODE_SUPPORT 1\n\n'
        static_impl_text = static_impl_text.replace('struct _utf_include_replace_struct{};\n', _read_all(_data_file("utf.hpp")))

    retval += static_impl_text
    retval += _read_all('concat_grammar.c')
    retval = retval.replace("#pragma once", '\n')
    return retval


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


def _render_lemon_input(grammar_file_path: str, **kwargs):
    '''
    Render the input meant for `lemon`.
    '''
    user_input = _read_all(grammar_file_path)
    mod = _extract_module(user_input)
    lexer_def = make_lexer(user_input, kwargs.get('use_unicode', False))
    codegen_text = f"%include {{\n{lexer_def}\n}}\n"

    header_text = _read_all(GRAMMAR_HEADER_FILE)

    return (mod, user_input + codegen_text + header_text)


def _write_build_lemon_grammar(whole_text: str):
    '''
    Write the input and call lemon to process it.
    '''
    _bootstrap_lemon()
    with open('concat_grammar.lemon', 'w') as f:
        f.write(whole_text)
    subprocess.check_call([LEMON, f"-T{LEMON_TEMPLATE}", "concat_grammar.lemon"])


def _render_buildable_module(grammar_file_path: str, **kwargs):
    '''
    Build the given module into a python module in the current directory.
    '''
    module_name, rendered_grammar = _render_lemon_input(grammar_file_path, **kwargs)
    _write_build_lemon_grammar(rendered_grammar)
    full_impl = _concatenate_implementation(**kwargs)
    with open('concat_grammar.c', 'w') as f:
        f.write(full_impl)
    return module_name


def _chdir_and_build(grammar_file_path, use_temp, **kwargs):
    grammar_file_path = os.path.abspath(grammar_file_path)
    old_dir = os.path.abspath(os.curdir)
    
    with tempfile.TemporaryDirectory() as workdir:
        if use_temp:
            os.chdir(workdir)

        grammar_module_name = _render_buildable_module(grammar_file_path, **kwargs)

        if kwargs.get('print_terminals', False):
            print_lang_header()

        if kwargs.get('cpp_dir', False):
            _copy_cpp_stuff(kwargs['cpp_dir'])
        elif not kwargs.get('no_build', False):
            subprocess.check_call(_gpp_command(grammar_module_name))
            if kwargs.get('install', False):
                soname = f"{grammar_module_name}.so"
                shutil.copy2(soname, os.path.join(site.getusersitepackages(), soname))

        os.chdir(old_dir)    

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

# --------------

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description="Build a grammar and optionally install it to the python path.")
    ap.add_argument('--unicode', default=False, const=True, action='store_const', help="Enable unicode support. This is necessary for reliable non-ASCII input, but increases memory usage in the resulting parser.")
    ap.add_argument('--cpp', type=str, required=False, help="Specify to output C++ compatible files to the indicated directory. Disables building the Python module.")
    ap.add_argument('--terminals', default=False, const=True, action='store_const', help="Print a skeleton `@lexdef` including all grammar-defined terminals.")
    ap.add_argument('--debug', default=False, const=True, action='store_const', help="Don't use a temp directory, dump everything in cwd.")
    ap.add_argument('--nobuild', default=False, const=True, action='store_const', help="Don't build the shared object. Bail beforehand.")
    ap.add_argument('--noinstall', default=False, const=True, action='store_const', help="Don't install the language, most useful with --debug.")
    ap.add_argument('grammar_file', type=str, help="The grammar file to build.")
    args = ap.parse_args()

    building_cpp = True if args.cpp else False

    func_args = { 
        'install' : not args.noinstall, 
        'use_unicode' : args.unicode, 
        'cpp_dir' : os.path.abspath(args.cpp) if building_cpp else None, 
        'suppress_python' : building_cpp,
        'no_build' : args.nobuild,
        'print_terminals' : args.terminals,
        'separate_interface' : building_cpp
    }

    _chdir_and_build(args.grammar_file, not args.debug, **func_args)

    