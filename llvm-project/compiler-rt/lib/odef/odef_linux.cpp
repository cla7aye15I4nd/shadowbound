#include "odef.h"
#include "odef_interface_internal.h"
#include "sanitizer_common/sanitizer_common.h"

#include <elf.h>
#include <link.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <unwind.h>

namespace __odef {

bool InitShadow() {
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i) {
    uptr start = kMemoryLayout[i].start;
    uptr end = kMemoryLayout[i].end;
    uptr size = end - start;
    
    if (kMemoryLayout[i].type == MappingDesc::SHADOW) {
      if (!MmapFixedSuperNoReserve(start, size, kMemoryLayout[i].name))
        return false;
    }
  }

  return true;
}


} // namespace __odef