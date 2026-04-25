@echo off
setlocal enabledelayedexpansion

set "parallel_jobs=%NUMBER_OF_PROCESSORS%"
if "%parallel_jobs%"=="" set "parallel_jobs=1"
set "build_type=Debug"
set "build_dir="
set "clean=0"
set "tls=0"
set "cmake_args="

:parse_args
if "%~1"=="" goto args_done
set "arg=%~1"
if /I "!arg!"=="-h" goto usage
if /I "!arg!"=="--help" goto usage
if /I "!arg!"=="-tls" set "tls=1" & shift & goto parse_args
if /I "!arg!"=="--tls" set "tls=1" & shift & goto parse_args
if /I "!arg!"=="-no-tls" set "tls=0" & shift & goto parse_args
if /I "!arg!"=="--no-tls" set "tls=0" & shift & goto parse_args
if /I "!arg!"=="-debug" set "build_type=Debug" & shift & goto parse_args
if /I "!arg!"=="--debug" set "build_type=Debug" & shift & goto parse_args
if /I "!arg!"=="-release" set "build_type=Release" & shift & goto parse_args
if /I "!arg!"=="--release" set "build_type=Release" & shift & goto parse_args
if /I "!arg!"=="-clean" set "clean=1" & shift & goto parse_args
if /I "!arg!"=="--clean" set "clean=1" & shift & goto parse_args
if /I "!arg!"=="-b" goto set_build_dir
if /I "!arg!"=="--build-dir" goto set_build_dir
if /I "!arg!"=="-j" goto set_jobs
if /I "!arg!"=="--jobs" goto set_jobs
if /I "!arg!"=="-DSYPHAX_WEB_ENABLE_TLS=ON" set "tls=1" & shift & goto parse_args
if /I "!arg!"=="-DSYPHAX_WEB_ENABLE_TLS=1" set "tls=1" & shift & goto parse_args
if /I "!arg!"=="-DSYPHAX_WEB_ENABLE_TLS=TRUE" set "tls=1" & shift & goto parse_args
if /I "!arg!"=="-DSYPHAX_WEB_ENABLE_TLS=OFF" set "tls=0" & shift & goto parse_args
if /I "!arg!"=="-DSYPHAX_WEB_ENABLE_TLS=0" set "tls=0" & shift & goto parse_args
if /I "!arg!"=="-DSYPHAX_WEB_ENABLE_TLS=FALSE" set "tls=0" & shift & goto parse_args
if "!arg:~0,2!"=="-D" set "cmake_args=!cmake_args! !arg!" & shift & goto parse_args
echo error: unknown option: !arg!
echo Run build.bat --help for usage.
exit /b 2

:set_build_dir
shift
if "%~1"=="" goto missing_build_dir
set "build_dir=%~1"
shift
goto parse_args

:set_jobs
shift
if "%~1"=="" goto missing_jobs
set "parallel_jobs=%~1"
shift
goto parse_args

:args_done
if "%build_dir%"=="" (
  if "%tls%"=="1" (
    set "build_dir=build-tls"
  ) else (
    set "build_dir=build"
  )
)

if "%tls%"=="1" (
  set "tls_value=ON"
) else (
  set "tls_value=OFF"
)

if not exist "lib\syphax\s_array.h" (
  git submodule update --init --recursive
  if errorlevel 1 exit /b 1
)

if "%clean%"=="1" (
  if exist "%build_dir%" rmdir /s /q "%build_dir%"
)

if not exist bin mkdir bin
set "bin_marker=bin\.syphax_web_active_build"
set "bin_config=build_dir=%build_dir%;build_type=%build_type%;tls=%tls_value%;cmake_args=%cmake_args%"
set "previous_bin_config="
if exist "%bin_marker%" set /p previous_bin_config=<"%bin_marker%"

if "%clean%"=="1" goto refresh_bin
if not "%previous_bin_config%"=="%bin_config%" goto refresh_bin
goto configure

:refresh_bin
echo Refreshing shared bin outputs for %build_dir% (%build_type%, TLS=%tls_value%)
del /q "bin\01_http.exe" "bin\02_https.exe" "bin\03_static_site.exe" "bin\04_live_queue.exe" "bin\05_folder_app.exe" "bin\syphax_web_tests.exe" >nul 2>nul

:configure
echo Configuring %build_dir% (%build_type%, TLS=%tls_value%)
cmake -S . -B "%build_dir%" ^
  -DCMAKE_BUILD_TYPE="%build_type%" ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON ^
  -DSYPHAX_WEB_BUILD_TESTS=ON ^
  -DSYPHAX_WEB_ENABLE_TLS="%tls_value%" ^
  %cmake_args%
if errorlevel 1 exit /b 1

echo Building %build_dir% with %parallel_jobs% job(s)
cmake --build "%build_dir%" --parallel %parallel_jobs%
if errorlevel 1 exit /b 1

>"%bin_marker%" echo %bin_config%
exit /b 0

:usage
echo Usage: build.bat [options] [cmake -D options]
echo.
echo Options:
echo   -tls, --tls          Build with OpenSSL TLS support. Uses build-tls by default.
echo   -no-tls, --no-tls    Build without TLS. Uses build by default.
echo   -debug, --debug      Build Debug. This is the default.
echo   -release, --release  Build Release.
echo   -b, --build-dir DIR  Use a custom build directory.
echo   -j, --jobs N         Build with N parallel jobs.
echo   -clean, --clean      Remove the selected build directory before configuring.
echo   -h, --help           Show this help.
exit /b 0

:missing_build_dir
echo error: missing build directory after -b
exit /b 2

:missing_jobs
echo error: missing job count after -j
exit /b 2
