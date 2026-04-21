@echo off
setlocal

set "parallel_jobs=%NUMBER_OF_PROCESSORS%"
if "%parallel_jobs%"=="" set "parallel_jobs=1"

git submodule update --init --recursive
if errorlevel 1 (
  echo Failed to fetch lib/syphax submodule.
  endlocal
  exit /b 1
)

if exist build (
  rmdir /s /q build
)

cmake -S . -B build ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON ^
  -DSYPHAX_WEB_BUILD_TESTS=ON
if errorlevel 1 (
  echo CMake configuration failed.
  endlocal
  exit /b 1
)

cmake --build build --parallel %parallel_jobs%
if errorlevel 1 (
  echo Build failed.
  endlocal
  exit /b 1
)

endlocal
exit /b 0
