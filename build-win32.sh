mkdir -p bin/
i686-w64-mingw32.static-gcc -o bin/result -Wall -Wextra -std=c99 -pedantic -Ofast -s src/*.c src/base64/*.c -Isrc/base64 src/stb_image/*.c -Isrc/stb_image `i686-w64-mingw32.static-pkg-config --libs --cflags sdl2 jasper libjpeg` -lm -pthread -DWANT_THREADS
