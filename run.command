#!/bin/bash
cd "$(dirname "$0")"
gcc -Wall -Wextra main.c -o signal_framework
./signal_framework
