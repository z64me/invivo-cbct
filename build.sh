mkdir -p bin/
gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Ofast -s src/*.c -ljasper
#gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Og -g src/*.c -ljasper
