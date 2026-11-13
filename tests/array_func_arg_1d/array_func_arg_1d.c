// MiniC program to test 1D array parameters
int vector_total(int a[10], int b[10], int n){
    int i;
    int total;
    i = 0;
    total = 0;

    while (i < n) {
        total = total + a[i] + b[i];
        i = i + 1;
    }

    return total;
}
