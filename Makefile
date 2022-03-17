FLAGS = -Wall -Wextra -pedantic -Og -ggdb
OUT = kilo
FILES = src/main.c src/map.c
main: $(FILES)
	$(CC) $(FILES)  -o $(OUT) $(FLAGS)
