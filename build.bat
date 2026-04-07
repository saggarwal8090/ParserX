@echo off
REM TinyLang Compiler — Windows Build Script
REM Run this from the project root directory.

REM Try to find gcc
where gcc >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: gcc not found in PATH.
    echo Please install MinGW-w64 and add it to your PATH, then re-run this script.
    echo Download: https://www.mingw-w64.org/
    echo.
    pause
    exit /b 1
)

echo Building tinylang_compiler.exe ...
gcc -Wall -Wextra -O2 -o tinylang_compiler.exe ^
    compiler/main.c ^
    compiler/lexer.c ^
    compiler/parser.c ^
    compiler/parse_table.c ^
    compiler/ast.c ^
    compiler/symbol_table.c ^
    compiler/optimizer.c ^
    compiler/json_output.c

if %ERRORLEVEL% equ 0 (
    echo.
    echo SUCCESS: tinylang_compiler.exe built successfully!
    echo.
    echo Test it:
    echo   echo int x = 3 + 4; | tinylang_compiler.exe
    echo.
) else (
    echo.
    echo BUILD FAILED. See errors above.
    echo.
)
pause
