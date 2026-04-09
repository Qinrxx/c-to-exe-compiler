# mycc — a tiny C compiler

`mycc` compiles a small subset of C directly to a Windows `.exe` via x86-64 AT&T assembly. Written in a single C++ file.

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
