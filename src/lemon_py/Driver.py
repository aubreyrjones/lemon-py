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
