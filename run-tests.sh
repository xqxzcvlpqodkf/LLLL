#!/usr/bin/env bash
# Builds and runs the GD-free test suite. No Geode SDK required.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
CXX="${CXX:-g++}"
"$CXX" -std=c++23 -O2 -Wall -Wextra -o build/learner_test tests/learner_test.cpp
./build/learner_test
