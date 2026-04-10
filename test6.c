#include <stdio.h>
int main(void) {
    char s[6];
    s[0] = 'h'; s[1] = 'e'; s[2] = 'l'; s[3] = 'l'; s[4] = 'o'; s[5] = 0;
    printf("%s\n", s);
    char c = 'A';
    c = c + 1;
    printf("%c\n", c);
    int i = 5;
    int j = i++;
    int k = ++i;
    printf("%d %d %d\n", i, j, k);
    return 0;
}
