CC 		= g++
CFLAGS 	= -Wall -Wextra -Werror -Wwrite-strings -pthread -std=c++17 -g
OPT 	=

all: driver

set: 
	$(CC) $(CFLAGS) $(OPT) -c set.h

sequential: set.o
	$(CC) $(CFLAGS) $(OPT) -c sequential.cpp

concurrent: set.o
	$(CC) $(CFLAGS) $(OPT) -c concurrent.cpp

transactional: set.o
	$(CC) $(CFLAGS) $(OPT) -c transactional.cpp

driver: sequential.o concurrent.o transactional.o driver.cpp
	$(CC) $(CFLAGS) $(OPT) -o driver driver.cpp

clean:
	rm -rf *.o driver