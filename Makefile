CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
INCLUDE = -Iinclude
SRC = src/main.c src/fs.c src/commands.c src/utils.c
OBJ = $(SRC:.c=.o)
BIN = bin
EXEC = filesys

all: $(EXEC)

$(EXEC): $(OBJ)
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ) -o $(BIN)/$(EXEC)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)/$(EXEC)

.PHONY: clean

