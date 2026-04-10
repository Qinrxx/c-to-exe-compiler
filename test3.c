#include <stdio.h>
int fib(int n) {
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n-1);
}
int main(void) {
    printf("fib10=%d\n", fib(10));
    printf("fact6=%d\n", fact(6));
    return 0;
}
