CC = gcc
CFLAGS = -Wall -Wextra -g

LIBRARY = libuthreads.a
HEADERS = uthreads.h
SOURCES = uthreads.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	ar rcs $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(LIBRARY)