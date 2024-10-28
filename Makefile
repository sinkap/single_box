# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Source files and executables
SRC = $(wildcard *.c)
EXEC = $(SRC:.c=)

# Special executable with liburing dependency
LIBURING_EXEC = http_server_io_uring
LIBURING_SRC = http_server_io_uring.c
LIBURING_FLAGS = -luring

# Default target builds all executables
all: $(EXEC)

# Rule to build each executable, with a special rule for liburing dependency
$(LIBURING_EXEC): $(LIBURING_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LIBURING_FLAGS)

# Pattern rule for other executables
%: %.c
	$(CC) $(CFLAGS) -o $@ $<

# Clean up generated files
clean:
	rm -f $(EXEC) *.o

# Create .gitignore, listing each executable on a new line
gitignore:
	@echo "*.o" > .gitignore
	@for exe in $(EXEC); do \
		echo "$$exe" >> .gitignore; \
	done

.PHONY: all clean gitignore
