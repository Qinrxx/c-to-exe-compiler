#include <stdio.h>
int main(void) {
    int sum = 0;
    int i;
    for (i = 1; i <= 100; i = i + 1) {
        if (i > 10) break;
        sum += i;
    }
    int j = 0;
    do { j = j + 2; } while (j < 8);
    printf("sum=%d j=%d\n", sum, j);
    return 0;
}
