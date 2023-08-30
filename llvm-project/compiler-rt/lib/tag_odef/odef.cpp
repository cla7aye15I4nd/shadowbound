#include "odef_interface_internal.h"
#include "sanitizer_common/sanitizer_common.h"

using namespace __sanitizer;

void __odef_init() {}

void __odef_report() {}

void __odef_abort() {
  Report(" Overflow detected\n");
  Die();
}
