#include "gc.h"

#define malloc(n) GC_MALLOC(n)
#define calloc(m,n) GC_MALLOC((m)*(n))
#define free(p) GC_SAFE_FREE(p)
#define realloc(p,n) GC_REALLOC((p),(n))
