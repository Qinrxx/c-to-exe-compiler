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

## Features

- `int` variables and arithmetic (`+ - * /`)
- Comparisons (`== != < > <= >=`)
- `if` / `else`, `while`
- Functions with up to 4 `int` parameters returning `int` (recursion works)
- `printf("fmt", args...)` with up to 3 int arguments
- `//` and `/* */` comments

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
- `test1.c / test2.c / test3.c` — sample programs
- `mycc.exe` — prebuilt statically-linked binary (Windows x64)

## Limitations

This is a teaching-sized compiler. Not supported: pointers, arrays, structs, `for` loops, `char`/`float`/other types, globals, preprocessor, multiple-file builds, >4 function args, >3 printf args.

## License

MIT
