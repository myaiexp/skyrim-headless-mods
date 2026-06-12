#!/bin/bash
# Compile the libei input client. No sudo, no -dev package beyond what ships
# with libei itself (header at /usr/include/libei-1.0/, resolved by pkg-config).
set -e
cd "$(dirname "$(readlink -f "$0")")"
gcc -O2 -Wall -o eidriver eidriver.c $(pkg-config --cflags --libs libei-1.0)
echo "built: $(pwd)/eidriver"
