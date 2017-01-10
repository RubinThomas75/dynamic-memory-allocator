CC = gcc
BIN = bin
BLD = build
SRC = src
INC = include
CFLAGS = -Wall -I$(INC) -g 
UTIL = $(BLD)/sfutil.o

.PHONY: all clean

all: create_dirs $(BIN)/sfalloc

create_dirs:
	@mkdir -p $(BIN)
	@mkdir -p $(BLD)


$(BIN)/sfalloc: $(SRC)/sfalloc.c $(BLD)/sfmm.o $(UTIL)
	$(CC) $(CFLAGS) $^ -o $@


$(BLD)/sfmm.o: $(SRC)/dymalloc.c $(UTIL)
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -rf $(BIN)
	rm -f $(BLD)/sfmm.o
