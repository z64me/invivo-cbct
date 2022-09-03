mkdir -p bin/
gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Ofast -s src/*.c src/base64/*.c -Isrc/base64 -ljasper -lSDL2 -lm
#gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Og -g src/*.c src/base64/*.c -Isrc/base64 -ljasper -lSDL2 -lm
