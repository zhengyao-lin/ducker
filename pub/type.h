#ifndef _PUB_TYPE_H_
#define _PUB_TYPE_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

typedef uint8_t byte_t;

#define ASSERT(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "assertion failed: %s: ", #cond); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
            abort(); \
        } \
    } while (0)

#endif
