#include "uuid.h"
#include "math/kmath.h"

#ifndef UUID_QUICK_AND_DIRTY
#define UUID_QUICK_AND_DIRTY
#endif

#ifndef UUID_QUICK_AND_DIRTY
#error "Full implementation of uuid does not exist"
#endif

void uuid_seed(u64 seed){
#ifdef UUID_QUICK_AND_DIRTY
// NOTE: does nothing, for now...
#endif
}

uuid uuid_generate() {
    uuid buf = {0};
#ifdef UUID_QUICK_AND_DIRTY
    // NOTE: this implementation does not guarantee any form of uniqueness as it just
    // uses random numbers. 
    // TODO: implement a real uuid generator.
    static char v[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            // Put a dash
            buf.value[i] = '-';
        } else {
            i32 offset = krandom() % 16;
            buf.value[i] = v[offset];
        }
    }
#endif

    // Make sure the string is terminated.
    buf.value[36] = '\0';
    return buf;
}