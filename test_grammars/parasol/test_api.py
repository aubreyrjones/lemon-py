import parasol_parser

with open('phong.prsl', 'r') as f:
    t = parasol_parser.parse(f.read())

for idx, c in enumerate(t[0][1]):
    print(repr(c))
