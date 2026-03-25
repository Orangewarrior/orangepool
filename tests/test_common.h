#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a); \
    int _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "ASSERT_EQ_INT failed: %d != %d (%s:%d)\n", _a, _b, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#endif
