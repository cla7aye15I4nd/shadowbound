#ifndef ODEF_INTERFACE_INTERNAL_H
#define ODEF_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __odef_init();

SANITIZER_INTERFACE_ATTRIBUTE
void __odef_report();

SANITIZER_INTERFACE_ATTRIBUTE __attribute__((noreturn))
void __odef_abort();

SANITIZER_INTERFACE_ATTRIBUTE
void __odef_deref(uptr addr);

}

#endif // ODEF_INTERFACE_INTERNAL_H