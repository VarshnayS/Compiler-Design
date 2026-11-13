// MiniC program to test addition using a local 2D array
int arr_addition(int n, int m) {
  int arr_result[10][5];
  int temp;

  arr_result[0][0] = m;
  arr_result[1][2] = n;
  temp = arr_result[0][0] + arr_result[1][2];

  return temp;
}
