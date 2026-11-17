// Forced into every host compile (build.sh -include): what newlib gives the
// Saturn build and glibc lacks.
#ifndef HOST_PLATFORM_H
#define HOST_PLATFORM_H
char* itoa(int value, char* str, int base);
#endif
