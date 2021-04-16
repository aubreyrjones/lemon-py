lemon/lemon: lemon/lemon.c
	gcc -O2 -o lemon/lemon lemon/lemon.c
	cp lemon/lemon src/lemon-py/lemon
	cp lemon/lempar.c src/lemon-py/lempar.c

swig: src/crust/ParseTree.cpp src/crust/ParseTree.hpp src/crust/ParseTree.i
	swig -I./src/crust -c++ -python -doxygen -py3 -o ./src/crust/ParseTree_py.cpp src/crust/ParseTree.i

object:
	g++ --std=c++17 -fPIC 

clean:
	rm -f lemon/lemon
	rm -f src/lemon-py/lemon
	rm src/crust/*.o
