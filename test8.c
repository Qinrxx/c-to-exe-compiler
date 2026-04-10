#include <stdio.h>
int sum6(int a, int b, int c, int d, int e, int f) {
    return a+b+c+d+e+f;
}
int main(void) {
    printf("%d\n", sum6(1,2,3,4,5,6));
    printf("%d %d %d %d %d %d %d\n", 1,2,3,4,5,6,7);
    return 0;
}
