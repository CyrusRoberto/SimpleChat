all: server

server: server.o
	gcc server.o -O2 -lpthread -o server

server.o: server.c
	gcc -c -O2 server.c

clean:
	rm -rf *.o server *~
