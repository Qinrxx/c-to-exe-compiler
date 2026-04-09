int main() {
    int i = 1;
    int sum = 0;
    while (i <= 10) {
        if (i == 5) {
            printf("skipping %d\n", i);
        } else {
            sum = sum + i;
        }
        i = i + 1;
    }
    printf("sum=%d\n", sum);
    return 0;
}
