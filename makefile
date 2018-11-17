
core: core.c
	gcc -g -o core -Wall -pedantic core.c -lpthread

clean:
	rm core
