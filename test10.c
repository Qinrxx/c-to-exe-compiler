#include <stdio.h>
int main(void) {
    int n = 0;
    int i = 0;
    while (1) {
        if (i >= 5) break;
        n = n + i;
        i = i + 1;
    }
    int hex = 0xff;
    int oct = 010;
    char nl = '\n';
    printf("n=%d hex=%d oct=%d nlcode=%d\n", n, hex, oct, nl);
    return 0;
}
