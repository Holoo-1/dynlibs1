#include "Max_coll.h"

int calculate(int arr[], int MATRIXSIZE) {
    int biggest = arr[0];
    for (int i = 0; i < MATRIXSIZE; i++) {
        if (biggest < arr[i]) {
            biggest = arr[i];
        }
    }
    return biggest;
}
