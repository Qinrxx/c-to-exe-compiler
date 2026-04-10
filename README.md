# mycc — a tiny C-to-EXE compiler

`mycc` compiles a small subset of C directly to a Windows `.exe` via x86-64 AT&T assembly. Written in a single C++ file.

> **This project was designed, written, debugged, and tested entirely by Claude (Anthropic's Opus 4.6 model) from a single prompt — no human wrote any of the source code.** The lexer, parser, code generator, Win64 ABI handling, test programs, build system, and this README were all produced by the model.

## How it was built

1. **Toolchain bootstrap** — installed MSYS2 + `mingw-w64-ucrt-x86_64-gcc` via `winget` so there was a working `g++` / `gcc` on Windows.
2. **Single-file C++ compiler** — `compiler.cpp` implements three classic stages:
   - **Lexer** — hand-written scanner producing a token stream (keywords, identifiers, numbers, string literals, operators, comments).
   - **Parser** — recursive-descent parser building a typed AST (`PROG → FUNC → BLOCK/STMT → EXPR`) with the usual precedence ladder (equality → relational → additive → multiplicative → unary → primary).
   - **Code generator** — walks the AST and emits x86-64 **AT&T-syntax** assembly following the **Windows x64 calling convention** (args in `RCX/RDX/R8/R9`, 32-byte shadow space, 16-byte stack alignment, `printf` linked from the CRT).
3. **Driver** — `mycc.exe` writes the `.s` file, invokes `gcc` to assemble and link, and cleans up. One command: `.c` → `.exe`.
4. **Tests** — three programs of increasing complexity (arithmetic, control flow, recursive functions) were compiled, linked, executed, and their output verified against expected results.

## Features (v2)

**Types**
- `int` (64-bit), `char` (1 byte), `void`
- Pointers (`T*`), fixed-size arrays (`T[N]`), pointer arithmetic with proper element scaling
- String literals, character literals (with `\n \t \r \0 \\ \' \" \xNN` escapes)
- Integer literals: decimal, hex (`0xff`), octal (`010`)

**Operators**
- Arithmetic: `+ - * / %`
- Bitwise: `& | ^ << >> ~`
- Logical: `&& || !` (short-circuit)
- Comparisons: `== != < > <= >=`
- Assignment: `= += -= *= /= %=`
- Increment/decrement: `++ --` (pre and post)
- Address-of `&`, dereference `*`, indexing `[]`, `sizeof`, ternary `?:`

**Control flow**
- `if` / `else`, `while`, `do`/`while`, `for`, `break`, `continue`, `return`
- Nested scopes with proper shadowing

**Functions**
- Any number of parameters (>4 spill to the stack per the Win64 ABI)
- Recursion, `int` / `char` / `void` / pointer returns
- Unknown function names are emitted as externs, so anything in the C runtime
  (`printf`, `puts`, `malloc`, …) just works via the linker — no special-casing.

**Globals**
- Scalar globals (zero-init or constant init)

**Preprocessor / misc**
- `#`-prefixed directives are silently skipped, so `#include <stdio.h>` is a no-op (the linker provides `printf`).
- `//` and `/* */` comments.

**Diagnostics**
- `file:line:col: error: …` with the source line and a caret pointing at the column.
- Errors accumulate so you see more than one per run.

## Pipeline

1. **Lexer** — tokenises source
2. **Parser** — builds an AST
3. **Code generator** — emits x86-64 AT&T assembly (Win64 ABI)
4. Invokes `gcc` to assemble + link into an `.exe`

## Requirements

- MinGW-w64 `gcc` on your `PATH` (easiest: install [MSYS2](https://www.msys2.org/), run `pacman -S mingw-w64-ucrt-x86_64-gcc`, add `C:\msys64\ucrt64\bin` to PATH).

## Usage

```
mycc hello.c                 # -> hello.exe
mycc hello.c -o myprog       # -> myprog.exe
mycc hello.c -S              # -> hello.s (stop after codegen)
mycc hello.c --keep-asm      # keep hello.s alongside hello.exe
mycc hello.c -g              # emit debug info (gdb can step by source line)
mycc hello.c --tokens        # dump lexer token stream
mycc hello.c --ast           # dump parsed AST
mycc hello.c -v              # verbose stage logging
```

### Debugging compiled programs

`mycc` can emit DWARF `.file` / `.loc` directives so `gdb` can step through your
source line-by-line:

```
mycc hello.c -g
gdb hello.exe
(gdb) break main
(gdb) run
(gdb) next        # step source line
(gdb) print x     # inspect a local
```

Combined with `--tokens` / `--ast`, you can inspect every compiler stage:

```
mycc hello.c --tokens --ast -v
```

### Example

```c
// hello.c
int square(int x) { return x * x; }

int main() {
    int i = 1;
    while (i <= 5) {
        printf("%d^2 = %d\n", i, square(i));
        i = i + 1;
    }
    return 0;
}
```

```
> mycc hello.c
wrote hello.exe
> hello
1^2 = 1
2^2 = 4
3^2 = 9
4^2 = 16
5^2 = 25
```

## Build from source

```
g++ -std=c++17 -O2 -static -o mycc.exe compiler.cpp
```

or `make`.

## Files

- `compiler.cpp` — the whole compiler
- `Makefile` — build rule
- `test1.c` … `test10.c` — test programs covering the v2 feature set
- `test*.expected` — expected stdout for each test
- `run_tests.bat` — compiles and runs every test, diffs output against expected
- `mycc.exe` — prebuilt statically-linked binary (Windows x64)

## Limitations

Still a teaching-sized compiler. Not supported: `struct`/`union`/`enum`,
floating-point, `typedef`, casts, function pointers, variadic function
*definitions* (calling variadics like `printf` is fine), multi-file builds,
a real preprocessor (`#include` is treated as a no-op).

## License

MIT
