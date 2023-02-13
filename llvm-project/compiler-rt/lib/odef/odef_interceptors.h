#ifndef ODEF_INTERCEPTORS_H
#define ODEF_INTERCEPTORS_H

#include "odef.h"
#include "interception/interception.h"

DECLARE_REAL(void*, memcpy, void *to, const void *from, uptr size)
DECLARE_REAL(void*, memset, void *block, int c, uptr size)

#endif // ODEF_INTERCEPTORS_H