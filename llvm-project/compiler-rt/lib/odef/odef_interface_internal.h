#ifndef ODEF_INTERFACE_INTERNAL_H
#define ODEF_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __odef_init();
void __odef_report();
void __odef_abort();
void __odef_set_shadow(uptr addr, uptr num, uptr size);

}

#endif // ODEF_INTERFACE_INTERNAL_H