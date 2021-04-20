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

from .BuildLexer import make_lexer

def data_file(*filename: str):
    return os.path.join(os.path.abspath(os.path.dirname(__file__)), *filename)

# this is the text injected to include the implementation
IMPL_TEXT = "\n#include <ParserImpl.cpp> // include the implementation file \n\n"
CPP_IMPL_TEXT = "\n"


GRAMMAR_HEADER_FILE = data_file("header.lemon")
LEMON = data_file("lemon")
LEMON_TEMPLATE = data_file("lempar.c")

def bootstrap_lemon():
    if os.path.isfile(data_file('lemon')):
        return
    
    print("Bootstrapping `lemon`.")
    command = ['gcc', '-O2', '-o', data_file('lemon'), data_file('lemon.c')]
    subprocess.check_call(command)



def copy_cpp_stuff(target_dir: str):
    if not os.path.isdir(target_dir):
        os.makedirs(target_dir)

    disable_python = '#define LEMON_PY_SUPPRESS_PYTHON 1\n\n'

    with open(data_file("ParseNode.hpp"), 'r') as pn:
        with open(os.path.join(target_dir, 'ParseNode.hpp'), 'w') as out:
            out.write(disable_python)
            out.write(pn.read())
    
    with open('concat_grammar.c', 'r') as parser:
        parser_text = parser.read()
    
    with open(data_file("ParserImpl.cpp"), 'r') as impl:
        impl_text = impl.read()
    with open('concat_grammar.h', 'r') as defines:
        impl_text = impl_text.replace('struct _to_be_replaced{};\n', defines.read())
    
    with open(os.path.join(target_dir, "_parser.cpp"), 'w') as outimpl:
        outimpl.write(impl_text + parser_text)


def gpp_command(module_name: str):
    pyinclude = subprocess.check_output(['python3-config', '--includes']).decode().strip()
    pylink = subprocess.check_output(['python3-config', '--ldflags']).decode().strip()

    retval = list('g++ -O3 -Wall -shared -std=c++17 -fPIC -fvisibility=hidden'.split())
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

def read_grammar_source(grammar_file_path: str):
    with open(os.path.abspath(grammar_file_path), 'r') as f:
        grammar_text = f.read()
    return grammar_text


def concatenate_input(grammar_text: str, impl_text: str):
    with open(GRAMMAR_HEADER_FILE, 'r') as f:
        header_text = f.read()

    lexer_def = make_lexer(grammar_text)
    codegen_text = f"%include {{\n{impl_text + lexer_def}\n}}\n"

    whole_text = grammar_text + "\n\n" + codegen_text + header_text

    return whole_text


def write_lemon_input(whole_text: str, grammar_module_name: str):
    with open('concat_grammar.lemon', 'w') as f:
        f.write(whole_text)
    subprocess.call([LEMON, f"-T{LEMON_TEMPLATE}", "concat_grammar.lemon"]) # f"-d{str(workdir)}",


def write_and_build_curdir(whole_text: str, grammar_module_name: str):
    write_lemon_input(whole_text, grammar_module_name)
    
    subprocess.call(gpp_command(grammar_module_name))


def extract_module(text: str):
    start = text.find('@pymod') # should look like maybe "//@pymod  \t foo_parser   "
    if start < 0:
        return "lemon_derived_parser" # that oughta lern 'em not to put a @pymod
    
    end = text.find("\n", start)

    linesplit = text[start:end].split()
    if len(linesplit) < 2:
        return "lemon_derived_parser"
    return linesplit[1].strip()

def lexdef_skeleton(tokname: str):
    justlen = 16 - len(tokname)
    return tokname + ":=".rjust(justlen) + " " + tokname.lower() + ":".rjust(justlen) + " [^\w_]"


def print_lang_header():
    with open('concat_grammar.h', 'r') as header_file:
        print("/*\n@pymod unnamed_language\n\n@lexdef\n\n!whitespace : \s+\n")
        lines = map(lambda l: lexdef_skeleton(l.split()[1].strip()), header_file.readlines())

        print("\n".join(lines))
        print("@endlex\n*/")


def build_grammar(grammar_file_path: str, install = False, use_temp = True, print_terminals = False, cpp_dir = None):
    bootstrap_lemon()

    grammar_file_path = os.path.abspath(grammar_file_path)
    print("Compiling: " + grammar_file_path)
    
    old_dir = os.path.abspath(os.curdir)

    grammar_text = read_grammar_source(grammar_file_path)
    grammar_module_name = extract_module(grammar_text)

    concatenated_text = concatenate_input(grammar_text, CPP_IMPL_TEXT if cpp_dir else IMPL_TEXT)

    with tempfile.TemporaryDirectory() as workdir:
        if use_temp:
            os.chdir(workdir)

        if cpp_dir:
            write_lemon_input(concatenated_text, grammar_module_name)
            copy_cpp_stuff(cpp_dir)
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
    ap.add_argument('--cpp', type=str, required=False, help="Specify to output C++ compatible files to the indicated directory. Disables building the Python module.")
    ap.add_argument('--terminals', default=False, const=True, action='store_const', help="Print a skeleton `@lexdef` including all grammar-defined terminals.")
    ap.add_argument('--debug', default=False, const=True, action='store_const', help="Don't use a temp directory, dump everything in cwd.")
    ap.add_argument('--noinstall', default=False, const=True, action='store_const', help="Don't install the language, most useful with --debug.")
    ap.add_argument('grammar_file', type=str, help="The grammar file to build.")
    args = ap.parse_args()

    build_grammar(args.grammar_file, not (args.noinstall or args.cpp), not args.debug, args.terminals, os.path.abspath(args.cpp) if args.cpp else None)