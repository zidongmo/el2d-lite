#include <stdio.h>

#include "PurismCore.h"

int main(void) {
    printf("purism_core_version=0x%08x\n", (unsigned)csmGetVersion());
    printf("purism_core_true_version=0x%08x\n", (unsigned)csmGetTrueVersion());
    printf("purism_core_latest_moc_version=%u\n", (unsigned)csmGetLatestMocVersion());
    return 0;
}
