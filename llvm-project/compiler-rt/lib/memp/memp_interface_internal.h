#ifndef SHADOWBOUND_INTERFACE_INTERNAL_H
#define SHADOWBOUND_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __shadowbound_init();

SANITIZER_INTERFACE_ATTRIBUTE
void __shadowbound_report();

SANITIZER_INTERFACE_ATTRIBUTE __attribute__((noreturn))
void __shadowbound_abort();

}

#endif // SHADOWBOUND_INTERFACE_INTERNAL_H