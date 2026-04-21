#!/bin/bash
set -eu

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON

cmake --build build
