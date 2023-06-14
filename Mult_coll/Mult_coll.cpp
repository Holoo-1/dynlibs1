#include "Mult_coll.h"

int MultOfColl(int arr[], int MATRIXSIZE) {
    
    int result = 1;
    for (int i = 0; i < MATRIXSIZE; i++) {
        result *= arr[i];
    }
    return result;
}

