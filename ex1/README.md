michael.gur
Michael Gur (207555178)
EX: 1

FILES:
osm.c -- implementation of osm.h
Makefile -- makefile for building the project
results.png -- graph of the results

REMARKS:

ANSWERS:

Assignment 1:
The program:
- creates 2 nested directories `./welcome_dir_1`, `./welcome_dir_1/welcome_dir_2/`, both with read, write & execute permissions for all users
- allocates 0x20000 bytes of memory
- creates a new file `./welcome_dir_1/welcome_dir_2/welcome_file.txt` with read/write permissions to all users, opens it in write-only mode
- opens `/etc/localtime` in read-only mode, reads and closes it
- writes the string "welcome to OS-2023" into `welcome_file.txt` and closes the file
- removes the file and both directories
- exists successfully
