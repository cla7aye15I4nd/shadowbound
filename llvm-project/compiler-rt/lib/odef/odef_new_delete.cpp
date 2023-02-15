#include "interception/interception.h"
#include "odef.h"
#include "sanitizer_common/sanitizer_common.h"

#include <stddef.h>

using namespace __odef;
namespace std {
struct nothrow_t {};
enum class align_val_t : size_t {};
} // namespace std

void ReportOutOfMemory() {
  Report("ERROR: overflow-defense: out of memory\n");
  Die();
}

#define OPERATOR_NEW_BODY(nothrow)                                             \
  void *res = odef_malloc(size);                                               \
  if (!nothrow && UNLIKELY(!res))                                              \
    ReportOutOfMemory();                                                       \
  return res

#define OPERATOR_NEW_BODY_ALIGN(nothrow)                                       \
  void *res = odef_memalign((uptr)align, size);                                \
  if (!nothrow && UNLIKELY(!res))                                              \
    ReportOutOfMemory();                                                       \
  return res;

INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size) { OPERATOR_NEW_BODY(false /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size) { OPERATOR_NEW_BODY(false /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size, std::nothrow_t const &) {
  OPERATOR_NEW_BODY(true /*nothrow*/);
}
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size, std::nothrow_t const &) {
  OPERATOR_NEW_BODY(true /*nothrow*/);
}
INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size, std::align_val_t align) {
  OPERATOR_NEW_BODY_ALIGN(false /*nothrow*/);
}
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size, std::align_val_t align) {
  OPERATOR_NEW_BODY_ALIGN(false /*nothrow*/);
}
INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size, std::align_val_t align,
                   std::nothrow_t const &) {
  OPERATOR_NEW_BODY_ALIGN(true /*nothrow*/);
}
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size, std::align_val_t align,
                     std::nothrow_t const &) {
  OPERATOR_NEW_BODY_ALIGN(true /*nothrow*/);
}

#define OPERATOR_DELETE_BODY                                                   \
  if (ptr)                                                                     \
  OdefDeallocate(ptr)

INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr) NOEXCEPT { OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr) NOEXCEPT { OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, std::nothrow_t const &) {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, std::nothrow_t const &) {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, size_t size) NOEXCEPT { OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, size_t size) NOEXCEPT {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, std::align_val_t align) NOEXCEPT {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, std::align_val_t align) NOEXCEPT {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, std::align_val_t align,
                     std::nothrow_t const &) {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, std::align_val_t align,
                       std::nothrow_t const &) {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, size_t size, std::align_val_t align) NOEXCEPT {
  OPERATOR_DELETE_BODY;
}
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, size_t size,
                       std::align_val_t align) NOEXCEPT {
  OPERATOR_DELETE_BODY;
}
