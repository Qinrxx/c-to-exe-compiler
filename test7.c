#include <stdio.h>
int g = 100;
int h;
int main(void) {
    h = 7;
    g += 5;
    h *= 3;
    printf("g=%d h=%d\n", g, h);
    return 0;
}
