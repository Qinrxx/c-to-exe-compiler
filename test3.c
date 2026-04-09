int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int fact(int n) {
    int r = 1;
    int i = 2;
    while (i <= n) {
        r = r * i;
        i = i + 1;
    }
    return r;
}

int main() {
    int n = 10;
    printf("fib(%d)=%d\n", n, fib(n));
    printf("fact(%d)=%d\n", n, fact(n));
    return 0;
}
