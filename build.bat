@echo off
setlocal

if not exist build (
  mkdir build
)

pushd build

cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
if errorlevel 1 (
  echo CMake configuration failed.
  popd
  endlocal
  exit /b 1
)

cmake --build . --config Debug
if errorlevel 1 (
  echo Build failed.
  popd
  endlocal
  exit /b 1
)

popd
echo Build succeeded.
endlocal
exit /b 0

