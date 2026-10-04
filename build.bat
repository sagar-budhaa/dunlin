@echo off
REM Local convenience only, not part of the deliverable.
REM Mirrors the Makefile gcc invocations for this Windows session.
REM Usage: build.bat lib ^| test ^| clean
setlocal
set CFLAGS=-std=c11 -Wall -Wextra -Werror -Wpedantic -Iinclude

if "%1"=="lib" (
    gcc %CFLAGS% -c src/arena.c -o src/arena.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/header.c -o src/header.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/request.c -o src/request.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/writer.c -o src/writer.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/mux.c -o src/mux.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/middleware.c -o src/middleware.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/conn.c -o src/conn.o
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -c src/server.c -o src/server.o
    if errorlevel 1 exit /b 1
    ar rcs libdunlin.a src/arena.o src/header.o src/request.o src/writer.o src/mux.o src/middleware.o src/conn.o src/server.o
    if errorlevel 1 exit /b 1
    echo lib-ok
    exit /b 0
)

if "%1"=="test" (
    call "%~f0" lib
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% tests/test_arena.c -L. -ldunlin -o tests/test_arena.exe
    if errorlevel 1 exit /b 1
    tests\test_arena.exe
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% tests/test_header.c -L. -ldunlin -o tests/test_header.exe
    if errorlevel 1 exit /b 1
    tests\test_header.exe
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% tests/test_parse.c -L. -ldunlin -o tests/test_parse.exe
    if errorlevel 1 exit /b 1
    tests\test_parse.exe
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -Isrc tests/test_writer.c -L. -ldunlin -o tests/test_writer.exe
    if errorlevel 1 exit /b 1
    tests\test_writer.exe
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -Isrc tests/test_mux.c -L. -ldunlin -o tests/test_mux.exe
    if errorlevel 1 exit /b 1
    tests\test_mux.exe
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% -Isrc tests/test_middleware.c -L. -ldunlin -o tests/test_middleware.exe
    if errorlevel 1 exit /b 1
    tests\test_middleware.exe
    exit /b %errorlevel%
)

if "%1"=="examples" (
    call "%~f0" lib
    if errorlevel 1 exit /b 1
    gcc %CFLAGS% examples/hello.c -L. -ldunlin -lws2_32 -o examples/hello.exe
    if errorlevel 1 exit /b 1
    echo examples-ok
    exit /b 0
)

if "%1"=="clean" (
    del /q src\*.o libdunlin.a tests\*.exe 2>nul
    del /q examples\*.exe 2>nul
    exit /b 0
)

echo usage: build.bat lib ^| test ^| clean ^| examples
exit /b 1
