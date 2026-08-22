#pragma once

#ifdef _WIN32
// Dummy getopt.h for Windows compatibility

#ifndef _GETOPT_DEFINED_
#define _GETOPT_DEFINED_

#ifdef __cplusplus
extern "C" {
#endif

extern char *optarg;
extern int optind, opterr, optopt;

inline int getopt(int argc, char * const argv[], const char *optstring) {
    return -1;
}

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument 0
#define required_argument 1
#define optional_argument 2

inline int getopt_long(int argc, char * const argv[], const char *optstring,
                       const struct option *longopts, int *longindex) {
    return -1;
}

#ifdef __cplusplus
}

// Define the variables to avoid linker errors if they are referenced
#ifdef _MSC_VER
__declspec(selectany) char *optarg = nullptr;
__declspec(selectany) int optind = 1, opterr = 1, optopt = 0;
#else
char *optarg __attribute__((weak)) = nullptr;
int optind __attribute__((weak)) = 1, opterr __attribute__((weak)) = 1, optopt __attribute__((weak)) = 0;
#endif

#endif

#endif // _GETOPT_DEFINED_

#endif
