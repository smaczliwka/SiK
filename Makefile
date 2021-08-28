CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -O2
LDFLAGS =

.PHONY: all clean

all: serwer

serwer: serwer.o err.o parser.o messenger.o
	$(CC) $(LDFLAGS) -o $@ $^

err.o: err.cc err.h
	$(CC) $(CFLAGS) -c $<

parser.o: parser.cc parser.h definitions.h
	$(CC) $(CFLAGS) -c $<

messenger.o: messenger.cc messenger.h
	$(CC) $(CFLAGS) -c $<
  
serwer.o: serwer.cc err.h parser.h messenger.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o serwer
