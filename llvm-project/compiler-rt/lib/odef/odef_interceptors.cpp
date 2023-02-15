#include "interception/interception.h"
#include "odef.h"

#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_errno_codes.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "sanitizer_common/sanitizer_vector.h"

using namespace __odef;

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !odef_inited; }
};

struct OdefInterceptorContext {
  bool in_interceptor_scope;
};

static THREADLOCAL int in_interceptor_scope;

struct InterceptorScope {
  InterceptorScope() { ++in_interceptor_scope; }
  ~InterceptorScope() { --in_interceptor_scope; }
};

bool IsInInterceptorScope() { return in_interceptor_scope; }

#define ENSURE_ODEF_INITED()                                                   \
  do {                                                                         \
    CHECK(!odef_init_is_running);                                              \
    if (!odef_inited) {                                                        \
      __odef_init();                                                           \
    }                                                                          \
  } while (0)

INTERCEPTOR(int, posix_memalign, void **memptr, SIZE_T alignment, SIZE_T size) {
  int res = odef_posix_memalign(memptr, alignment, size);
  return res;
}

INTERCEPTOR(void *, memalign, SIZE_T alignment, SIZE_T size) {
  return odef_memalign(alignment, size);
}

INTERCEPTOR(void *, aligned_alloc, SIZE_T alignment, SIZE_T size) {
  return odef_aligned_alloc(alignment, size);
}

INTERCEPTOR(void *, __libc_memalign, SIZE_T alignment, SIZE_T size) {
  void *ptr = odef_memalign(alignment, size);
  if (ptr)
    DTLS_on_libc_memalign(ptr, size);
  return ptr;
}

INTERCEPTOR(void *, valloc, SIZE_T size) { return odef_valloc(size); }

INTERCEPTOR(void *, pvalloc, SIZE_T size) { return odef_pvalloc(size); }

INTERCEPTOR(void, free, void *ptr) {
  if (UNLIKELY(!ptr))
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  OdefDeallocate(ptr);
}

INTERCEPTOR(void, cfree, void *ptr) {
  if (UNLIKELY(!ptr))
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  OdefDeallocate(ptr);
}

INTERCEPTOR(void *, malloc, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  return odef_malloc(size);
}

#define ODEF_INTERCEPT_FUNC(name)                                              \
  do {                                                                         \
    if (!INTERCEPT_FUNCTION(name))                                             \
      VReport(1, "OverflowDefense: failed to intercept '%s'\n", #name);        \
  } while (0)

#define ODEF_INTERCEPT_FUNC_VER(name, ver)                                     \
  do {                                                                         \
    if (!INTERCEPT_FUNCTION_VER(name, ver))                                    \
      VReport(1, "OverflowDefense: failed to intercept '%s@@%s'\n", #name,     \
              ver);                                                            \
  } while (0)
#define ODEF_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)                \
  do {                                                                         \
    if (!INTERCEPT_FUNCTION_VER(name, ver) && !INTERCEPT_FUNCTION(name))       \
      VReport(1, "OverflowDefense: failed to intercept '%s@@%s' or '%s'\n",    \
              #name, ver, #name);                                              \
  } while (0)

#define DO_NOTHING                                                             \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPT_FUNCTION(name) ODEF_INTERCEPT_FUNC(name)
#define COMMON_INTERCEPT_FUNCTION_VER(name, ver)                               \
  ODEF_INTERCEPT_FUNC_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver)          \
  ODEF_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#define COMMON_INTERCEPTOR_UNPOISON_PARAM(count) DO_NOTHING
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size) DO_NOTHING
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(ptr, size) DO_NOTHING
#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size) DO_NOTHING
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(ptr, size) DO_NOTHING
#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)                               \
  if (odef_init_is_running)                                                    \
    return REAL(func)(__VA_ARGS__);                                            \
  ENSURE_ODEF_INITED();                                                        \
  OdefInterceptorContext odef_ctx = {IsInInterceptorScope()};                  \
  ctx = (void *)&odef_ctx;                                                     \
  (void)ctx;                                                                   \
  InterceptorScope interceptor_scope;
#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path)                              \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) DO_NOTHING
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) DO_NOTHING
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) DO_NOTHING
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) DO_NOTHING
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name) DO_NOTHING
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
#define COMMON_INTERCEPTOR_ON_EXIT(ctx) (0)

#include "sanitizer_common/sanitizer_common_interceptors.inc"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

struct OdefAtExitRecord {
  void (*func)(void *arg);
  void *arg;
};

struct InterceptorContext {
  Mutex atexit_mu;
  Vector<struct OdefAtExitRecord *> AtExitStack;

  InterceptorContext() : AtExitStack() {}
};

static ALIGNED(64) char interceptor_placeholder[sizeof(InterceptorContext)];
InterceptorContext *interceptor_ctx() {
  return reinterpret_cast<InterceptorContext *>(&interceptor_placeholder[0]);
}
namespace __odef {

void InitializeInterceptors() {
  static int inited = 0;
  CHECK_EQ(inited, 0);

  new (interceptor_ctx()) InterceptorContext();

  InitializeCommonInterceptors();

  INTERCEPT_FUNCTION(posix_memalign);
  INTERCEPT_FUNCTION(memalign);
  INTERCEPT_FUNCTION(aligned_alloc);
  INTERCEPT_FUNCTION(__libc_memalign);
  INTERCEPT_FUNCTION(valloc);
  INTERCEPT_FUNCTION(pvalloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(cfree);
  INTERCEPT_FUNCTION(malloc);

  inited = 1;
}

} // namespace __odef
