lemon/lemon: lemon/lemon.c
	gcc -O2 -o lemon/lemon lemon/lemon.c
	cp lemon/lemon src/lemon_py/lemon
	cp lemon/lempar.c src/lemon_py/lempar.c

clean:
	rm -f lemon/lemon
	rm -f src/lemon_py/lemon
	rm -f src/lemon_py/lempar.c
