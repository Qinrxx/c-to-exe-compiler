#include <stdio.h>
int main(void) {
    int x = 10;
    {
        int x = 20;
        printf("%d\n", x);
    }
    printf("%d\n", x);
    int y = (x > 5) ? 111 : 222;
    printf("%d\n", y);
    int a[3];
    printf("%d %d\n", sizeof(int), sizeof(a));
    return 0;
}
