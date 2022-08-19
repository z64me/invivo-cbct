mkdir -p bin/
i686-w64-mingw32.static-gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Ofast -s src/*.c `i686-w64-mingw32.static-pkg-config --libs --cflags sdl2 jasper libjpeg` -lm
