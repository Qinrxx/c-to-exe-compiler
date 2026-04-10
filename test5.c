#include <stdio.h>
int main(void) {
    int a[5];
    int i;
    for (i = 0; i < 5; i = i + 1) a[i] = i * i;
    int *p = a;
    int s = 0;
    for (i = 0; i < 5; i = i + 1) s += *(p + i);
    printf("s=%d a4=%d\n", s, a[4]);
    return 0;
}
