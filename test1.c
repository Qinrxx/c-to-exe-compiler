#include <stdio.h>
int main(void) {
    int a = 17;
    int b = 5;
    printf("%d %d %d %d %d\n", a+b, a-b, a*b, a/b, a%b);
    printf("%d %d %d %d %d\n", a&b, a|b, a^b, a<<2, a>>1);
    printf("%d %d %d\n", -a, ~a, !0);
    return 0;
}
