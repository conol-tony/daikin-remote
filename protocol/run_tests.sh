#!/usr/bin/env sh
# Compile the protocol encoder on the host (C99 and C++17) and run the tests.
set -eu
cd "$(dirname "$0")"
mkdir -p .build
gcc -std=c99 -O1 -Wall -Wextra -Wpedantic -Wconversion -Werror -I. \
    daikin_arc466.c test/test_daikin.c -o .build/test_daikin_c
./.build/test_daikin_c
# Make sure the header is usable from C++ too (test compiled as C++,
# encoder compiled as C, linked together).
gcc -std=c99 -O1 -Wall -Wextra -Werror -I. -c daikin_arc466.c -o .build/daikin_arc466.o
g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. -x c++ test/test_daikin.c \
    -x none .build/daikin_arc466.o -o .build/test_daikin_cpp
./.build/test_daikin_cpp
