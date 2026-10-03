#include "odef_interface_internal.h"
#include "sanitizer_common/sanitizer_common.h"

using namespace __sanitizer;

void __shadowbound_init() {}

void __shadowbound_report() {}

void __shadowbound_abort() {
  Report(" Overflow detected\n");
  Die();
}
