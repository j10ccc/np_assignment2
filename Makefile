
all: libcalc test client server serverD



servermain.o: servermain.cpp protocol.h
	$(CXX) -Wall -c servermain.cpp -I. -std=c++11

servermainD.o: servermain.cpp protocol.h
	$(CXX) -Wall -c servermain.cpp -I. -DDEBUG -o servermainD.o -std=c++11


clientmain.o: clientmain.cpp protocol.h
	$(CXX) -Wall -c clientmain.cpp -I. -std=c++11

main.o: main.cpp protocol.h
	$(CXX) -Wall -c main.cpp -I.


test: main.o calcLib.o
	$(CXX) -L./ -Wall -o test main.o -lcalc

client: clientmain.o calcLib.o
	$(CXX) -L./ -Wall -o client clientmain.o -lcalc

server: servermain.o calcLib.o
	$(CXX) -L./ -Wall -o server servermain.o -lcalc

serverD: servermainD.o calcLib.o
	$(CXX) -L./ -Wall -o serverD servermainD.o -lcalc 



calcLib.o: calcLib.c calcLib.h
	gcc -Wall -fPIC -c calcLib.c

libcalc: calcLib.o
	ar -rc libcalc.a calcLib.o

clean:
	rm *.o *.a test server serverD client
