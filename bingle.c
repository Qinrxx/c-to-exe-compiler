int square(int x) {
    return x * x;
}

int main() {
    int i = 1;
    while (i <= 5) {
        printf("bingle %d squared = %d\n", i, square(i));
        i = i + 1;
    }
    return 0;
}
