import subprocess
import tempfile
import os.path
import os


LEMON = os.path.join(os.path.dirname(__file__), "lemon")
LEMON_TEMPLATE = os.path.join(os.path.dirname(__file__), "lempar.c")

def build_grammar(grammar_file_path: str):
    grammar_file_path = os.path.abspath(grammar_file_path)
    print("Compiling: " + grammar_file_path)
    outfile = os.path.splitext(os.path.basename(grammar_file_path))[0] + ".c"
    with tempfile.TemporaryDirectory() as workdir:
        outfile = os.path.join(workdir, outfile)
        subprocess.call([LEMON, f"-T{LEMON_TEMPLATE}", f"-d{str(workdir)}", grammar_file_path])
        print(os.listdir(workdir))
    #call `lemon grammar_file_path`
    pass

if __name__ == '__main__':
    import sys
    build_grammar(sys.argv[1])