# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Source files and executables
SRC = $(wildcard *.c)
EXEC = $(SRC:.c=)

# Default target
all: $(EXEC)

# Rule to build each executable
%: %.c
	$(CC) $(CFLAGS) -o $@ $<

# Clean up generated files
clean:
	rm -f $(EXEC) *.o

# Include a .gitignore file
gitignore:
	@echo "*.o" > .gitignore
	@for exe in $(EXEC); do \
		echo "$$exe" >> .gitignore; \
	done

.PHONY: all clean gitignore