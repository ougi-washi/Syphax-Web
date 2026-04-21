#!/bin/bash
set -eu

parallel_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
if [ -z "${parallel_jobs}" ] || [ "${parallel_jobs}" -lt 1 ] 2>/dev/null; then
  parallel_jobs=1
fi

git submodule update --init --recursive
rm -rf build

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON

cmake --build build --parallel "${parallel_jobs}"
