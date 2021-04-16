lemon/lemon: lemon/lemon.c
	gcc -O2 -o lemon/lemon lemon/lemon.c
	cp lemon/lemon src/lemon-py/lemon
	cp lemon/lempar.c src/lemon-py/lempar.c

object:
	g++ --std=c++17 -fPIC 

clean:
	rm -f lemon/lemon
	rm -f src/lemon-py/lemon
	rm src/crust/*.o
