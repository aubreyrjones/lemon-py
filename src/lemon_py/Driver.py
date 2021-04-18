import importlib
import argparse
import tempfile
import subprocess
from os import path

class Driver:
    def __init__(self, lang_name: str):
        importlib.invalidate_caches()
        mod = importlib.import_module(lang_name)
        self._parse_fn = getattr(mod, 'parse')
        self._dot_fn = getattr(mod, 'dotify')

    def parse(self, instr: str):
        return self._parse_fn(instr)
    
    def dotify(self, parse_tree):
        return self._dot_fn(parse_tree)

    def write_dot(self, parse_tree, outfile_path):
        with open(outfile_path, 'w') as f:
            f.write(self.dotify(parse_tree))
    
    def vis_dot(self, parse_tree):
        with tempfile.TemporaryDirectory() as workdir:
            outfile = path.join(workdir, "out.dot")
            pngfile = f"{outfile}.png"
            self.write_dot(parse_tree, outfile)
            subprocess.call(["dot", "-Tpng", f"-o{pngfile}", outfile]) # dot -Tpng -O out.dot && display out.dot.png
            subprocess.call(["display", pngfile])


if __name__ == '__main__':
    import argparse
    import sys
    ap = argparse.ArgumentParser(description="Build a grammar and optionally install it to the python path.")
    ap.add_argument('--vis', default=False, const=True, action='store_const', help="Visualize with dot.")
    ap.add_argument('--dot', type=str, help="Dot output file.")
    ap.add_argument('language', type=str, help="Language module name to use.")
    ap.add_argument('input_file', type=str, help="Input file to parser.")
    args = ap.parse_args()

    d = Driver(args.language)

    infile = sys.stdin
    if args.input_file != '0':
        infile = open(args.input_file, 'r')
        
    parse_tree = d.parse(infile.read())
    
    if args.input_file != '0':
        infile.close()
    
    if args.dot:
        d.write_dot(parse_tree, args.dot)
    
    if args.vis:
        d.vis_dot(parse_tree)