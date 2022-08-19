mkdir -p bin/
gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Ofast -s src/*.c -ljasper -lSDL2 -lm
#gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Og -g src/*.c -ljasper -lSDL2 -lm
