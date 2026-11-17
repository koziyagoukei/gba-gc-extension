#ifndef GUARD_ALLOC_H
#define GUARD_ALLOC_H
#include <stdlib.h>
#define AllocUnchecked(size) malloc(size)
#define AllocZeroedUnchecked(size) calloc(1, size)
#define Free(p) free(p)
#define TRY_FREE_AND_SET_NULL(p) do { free(p); (p) = NULL; } while (0)
#endif
