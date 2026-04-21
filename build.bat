@echo off
setlocal

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

cmake --build build
if errorlevel 1 (
  echo Build failed.
  endlocal
  exit /b 1
)

endlocal
exit /b 0
