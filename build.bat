@echo off
REM Local convenience only, not part of the deliverable.
REM Mirrors the Makefile gcc invocations for this Windows session.
REM Usage: build.bat lib ^| test ^| clean
setlocal
set CFLAGS=-std=c11 -Wall -Wextra -Werror -Wpedantic -Iinclude

if "%1"=="lib" (
    gcc %CFLAGS% -c src/arena.c -o src/arena.o
    if errorlevel 1 exit /b 1
    ar rcs libdunlin.a src/arena.o
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
    exit /b %errorlevel%
)

if "%1"=="clean" (
    del /q src\*.o libdunlin.a tests\*.exe 2>nul
    exit /b 0
)

echo usage: build.bat lib ^| test ^| clean
exit /b 1
