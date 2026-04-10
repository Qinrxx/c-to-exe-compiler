@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not exist mycc.exe (
    echo mycc.exe not found - run "make" first.
    exit /b 1
)

set PASS=0
set FAIL=0

for %%T in (test1 test2 test3 test4 test5 test6 test7 test8 test9 test10) do (
    .\mycc.exe %%T.c -o %%T >nul 2>&1
    if errorlevel 1 (
        echo [FAIL] %%T - compile error
        set /a FAIL+=1
    ) else (
        .\%%T.exe > %%T.actual 2>&1
        fc /n %%T.expected %%T.actual >nul 2>&1
        if errorlevel 1 (
            echo [FAIL] %%T - output mismatch
            echo --- expected ---
            type %%T.expected
            echo --- actual ---
            type %%T.actual
            set /a FAIL+=1
        ) else (
            echo [PASS] %%T
            set /a PASS+=1
        )
        del %%T.actual >nul 2>&1
    )
)

echo.
echo Passed: !PASS!  Failed: !FAIL!
if !FAIL! gtr 0 exit /b 1
exit /b 0
