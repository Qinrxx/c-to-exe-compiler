#include <stdio.h>
int main(void) {
    int x = 7;
    int y = 3;
    if (x > y && y > 0) printf("a\n");
    if (x < y || x == 7) printf("b\n");
    if (!(x == y)) printf("c\n");
    int i;
    for (i = 0; i < 3; i = i + 1) {
        if (i == 1) continue;
        printf("i=%d\n", i);
    }
    return 0;
}
