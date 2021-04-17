import subprocess
import tempfile
import os.path
import os
import pybind11

import BuildLexer

def data_file(*filename: str):
    return os.path.join(os.path.abspath(os.path.dirname(__file__)), *filename)


GRAMMAR_HEADER_FILE = data_file("header.lemon")
PARSER_IMPL_FILE = data_file("ParserImpl.cpp")

LEMON = data_file("lemon")
LEMON_TEMPLATE = data_file("lempar.c")

def gpp_command(module_name: str):
    pyinclude = subprocess.check_output(['python3-config', '--includes']).decode().strip()
    pylink = subprocess.check_output(['python3-config', '--ldflags']).decode().strip()

    retval = list('g++ -O3 -Wall -shared -std=c++17 -fPIC'.split())
    retval.extend(pyinclude.split())
    retval.extend(pylink.split())
    retval.extend([
        f"-DPYTHON_PARSER_MODULE_NAME={module_name}",
        f"-I{pybind11.get_include()}",
        "concat_grammar.c",
        "-o",
        f"{module_name}.so"
    ])
    

    return retval

def read_grammar_source(grammar_file_path: str):
    with open(os.path.abspath(grammar_file_path), 'r') as f:
        grammar_text = f.read()
    return grammar_text


def concatenate_input(grammar_text: str):
    with open(GRAMMAR_HEADER_FILE, 'r') as f:
        header_text = f.read()
    
    with open(PARSER_IMPL_FILE, 'r') as f:
        impl_text = f.read()

    lexer_def = BuildLexer.make_lexer(grammar_text)

    whole_text = header_text + "\n" + f"%include {{\n{impl_text + lexer_def}\n}}\n" + grammar_text

    return whole_text


def write_and_build_curdir(whole_text: str, grammar_module_name: str):
    with open('concat_grammar.lemon', 'w') as f:
        f.write(whole_text)
    
    subprocess.call([LEMON, f"-T{LEMON_TEMPLATE}", "concat_grammar.lemon"]) # f"-d{str(workdir)}",
    
    subprocess.call(gpp_command(grammar_module_name))


def build_grammar(grammar_file_path: str, grammar_module_name: str, use_temp = True):
    grammar_file_path = os.path.abspath(grammar_file_path)
    print("Compiling: " + grammar_file_path)
    
    old_dir = os.path.abspath(os.curdir)
    
    grammar_text = read_grammar_source(grammar_file_path)

    with tempfile.TemporaryDirectory() as workdir:
        if use_temp:
            os.chdir(workdir)
        write_and_build_curdir(concatenate_input(grammar_text), grammar_module_name)
        os.chdir(old_dir)    
    
    pass

if __name__ == '__main__':
    import sys
    build_grammar(sys.argv[1], 'lemon_parser', False)