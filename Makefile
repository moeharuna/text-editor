FLAGS = -Wall -Wextra -pedantic -Og -ggdb
OUT = kilo
main: src/main.c
	$(CC) src/main.c -o $(OUT) $(FLAGS)
