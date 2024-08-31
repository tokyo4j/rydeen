// inspired by https://github.com/tezc/sc
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_DEF(T, name) \
    struct array_##name { \
        size_t cap; \
        size_t size; \
        T *elems; \
    }

ARRAY_DEF(int, int);
ARRAY_DEF(void *, ptr);
ARRAY_DEF(uint32_t, u32);

#define array_add(arr, elem) \
    do { \
        if ((arr)->cap == (arr)->size) { \
            if (!(arr)->cap) { \
                (arr)->cap = 1; \
                (arr)->elems = malloc(sizeof(*(arr)->elems)); \
            } else { \
                (arr)->cap *= 2; \
                (arr)->elems = \
                    realloc((arr)->elems, (arr)->cap * sizeof(*(arr)->elems)); \
            } \
        } \
        (arr)->elems[(arr)->size++] = (elem); \
    } while (0)

#define array_foreach(arr, entry) \
    for (size_t __i = 0, __flag = 1, __size = (arr)->size; \
         __flag && __i < __size; __flag = !__flag, __i++) \
        for (typeof(*(arr)->elems) entry = (arr)->elems[__i]; __flag; \
             __flag = !__flag)

#define array_foreach_ref(arr, entry) \
    for (size_t __i = 0, __flag = 1, __size = (arr)->size; \
         __flag && __i < __size; __flag = !__flag, __i++) \
        for (typeof((arr)->elems) entry = &(arr)->elems[__i]; __flag; \
             __flag = !__flag)

#define array_foreach_reverse(arr, entry) \
    for (size_t __i = (arr)->size - 1, __flag = 1; \
         __flag && __i != (size_t) - 1; __flag = !__flag, __i--) \
        for (typeof(*(arr)->elems) entry = (arr)->elems[__i]; __flag; \
             __flag = !__flag)

#define array_foreach_reverse_ref(arr, entry) \
    for (size_t __i = (arr)->size - 1, __flag = 1; \
         __flag && __i != (size_t) - 1; __flag = !__flag, __i--) \
        for (typeof((arr)->elems) entry = &(arr)->elems[__i]; __flag; \
             __flag = !__flag)

#define array_remove(arr, idx) \
    do { \
        (arr)->size--; \
        memmove(&(arr)->elems[idx], &(arr)->elems[idx + 1], \
                ((arr)->size - idx) * sizeof(*(arr)->elems)); \
    } while (0)

#define array_reverse(arr) \
    do { \
        if (!(arr)->elems) \
            break; \
        typeof(*(arr)->elems) __buf; \
        for (typeof((arr)->elems) __p = (arr)->elems, \
                                  __q = (arr)->elems + (arr)->size - 1; \
             __p < __q; __p++, __q--) { \
            __buf = *__p; \
            *__p = *__q; \
            *__q = __buf; \
        } \
    } while (0)

#define array_concat(arr1, arr2) \
    do { \
        array_foreach(arr2, __elem) { array_add(arr1, __elem); } \
    } while (0)

#define array_free(arr) \
    do { \
        if ((arr)->elems) \
            free((arr)->elems); \
        *(arr) = (typeof(*arr)){0}; \
    } while (0)
