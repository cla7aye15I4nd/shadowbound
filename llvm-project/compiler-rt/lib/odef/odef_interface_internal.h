#ifndef ODEF_INTERFACE_INTERNAL_H
#define ODEF_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __odef_init();
void __odef_report();
}

#endif // ODEF_INTERFACE_INTERNAL_H