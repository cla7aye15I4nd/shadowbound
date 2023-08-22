#include "interception/interception.h"
#include "odef.h"
#include "odef_thread.h"

#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_errno_codes.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "sanitizer_common/sanitizer_vector.h"

using namespace __odef;

DECLARE_REAL(void *, memset, void *dest, int c, uptr n)
DECLARE_REAL(void *, memcpy, void *src, const void* dst, uptr n)

static THREADLOCAL int in_interceptor_scope;

struct InterceptorScope {
  InterceptorScope() { ++in_interceptor_scope; }
  ~InterceptorScope() { --in_interceptor_scope; }
};

bool IsInInterceptorScope() { return in_interceptor_scope; }

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !odef_inited; }
};

struct OdefInterceptorContext {
  bool in_interceptor_scope;
};

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

INTERCEPTOR(void *, calloc, SIZE_T nmemb, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  return odef_calloc(nmemb, size);
}

INTERCEPTOR(void *, realloc, void *ptr, SIZE_T size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  return odef_realloc(ptr, size);
}

INTERCEPTOR(void *, reallocarray, void *ptr, SIZE_T nmemb, SIZE_T size) {
  return odef_reallocarray(ptr, nmemb, size);
}

INTERCEPTOR(void *, malloc, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  return odef_malloc(size);
}

INTERCEPTOR(void, mallinfo, __sanitizer_struct_mallinfo *sret) {
  REAL(memset)(sret, 0, sizeof(*sret));
}

INTERCEPTOR(int, mallopt, int cmd, int value) { return 0; }

INTERCEPTOR(void, malloc_stats, void) {
  // FIXME: implement, but don't call REAL(malloc_stats)!
}

INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  return odef_allocated_size(ptr);
}

extern "C" int pthread_attr_init(void *attr);
extern "C" int pthread_attr_destroy(void *attr);

static void *OdefThreadStartFunc(void *arg) {
  OdefThread *t = (OdefThread *)arg;
  SetCurrentThread(t);
  t->Init();
  SetSigProcMask(&t->starting_sigset_, nullptr);
  return t->ThreadStart();
}

INTERCEPTOR(int, pthread_create, void *th, void *attr,
            void *(*callback)(void *), void *param) {
  ENSURE_ODEF_INITED(); // for GetTlsSize()
  __sanitizer_pthread_attr_t myattr;
  if (!attr) {
    pthread_attr_init(&myattr);
    attr = &myattr;
  }

  AdjustStackSize(attr);

  OdefThread *t = OdefThread::Create(callback, param);
  ScopedBlockSignals block(&t->starting_sigset_);
  int res = REAL(pthread_create)(th, attr, OdefThreadStartFunc, t);

  if (attr == &myattr)
    pthread_attr_destroy(&myattr);
  return res;
}

INTERCEPTOR(int, pthread_join, void *th, void **retval) {
  ENSURE_ODEF_INITED();
  int res = REAL(pthread_join)(th, retval);
  return res;
}

DEFINE_REAL_PTHREAD_FUNCTIONS

extern "C" SANITIZER_WEAK_ATTRIBUTE const int __odef_only_small_alloc_opt;
extern "C" SANITIZER_WEAK_ATTRIBUTE const int __odef_keep_going;
extern "C" SANITIZER_WEAK_ATTRIBUTE const int __odef_skip_instrument;

void check_range(uptr ptr, uptr size) {
  if (__odef_skip_instrument || !MEM_IS_APP(ptr))
    return;
  if (__builtin_expect(__odef_only_small_alloc_opt, 1)) {
    if (((uptr)(*((u32 *)MEM_TO_SHADOW(ptr)))) < size) {
      Report(" Overflow detected (check_range)\n");
      if (!__odef_keep_going)
        Die();
    }
  } else {
    if (((uptr)(*((u32 *)MEM_TO_SHADOW(ptr)))) * sizeof(u32) < size) {
      Report(" Overflow detected (check_range)\n");
      if (!__odef_keep_going)
        Die();
    }
  }
}

#define ODEF_CHECK_RANGE(ptr, size) check_range((uptr)(ptr), (uptr)(size))

INTERCEPTOR(char *, strdup, const char *s) {
  if (UNLIKELY(!odef_inited))
    return internal_strdup(s);

  uptr length = internal_strlen(s);
  ODEF_CHECK_RANGE(s, length + 1);

  void *new_mem = odef_malloc(length + 1);
  REAL(memcpy)(new_mem, s, length + 1);
  return reinterpret_cast<char *>(new_mem);
}

INTERCEPTOR(char *, __strdup, const char *s) {
  if (UNLIKELY(!odef_inited))
    return internal_strdup(s);

  uptr length = internal_strlen(s);
  ODEF_CHECK_RANGE(s, length + 1);

  void *new_mem = odef_malloc(length + 1);
  REAL(memcpy)(new_mem, s, length + 1);
  return reinterpret_cast<char *>(new_mem);
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

#define COMMON_INTERCEPT_FUNCTION(name) ODEF_INTERCEPT_FUNC(name)
#define COMMON_INTERCEPT_FUNCTION_VER(name, ver)                               \
  ODEF_INTERCEPT_FUNC_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver)          \
  ODEF_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#define COMMON_INTERCEPTOR_UNPOISON_PARAM(count)                               \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size)                         \
  do {                                                                         \
    ODEF_CHECK_RANGE(ptr, size);                                               \
  } while (0)
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(ptr, size)                         \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size)                          \
  do {                                                                         \
    ODEF_CHECK_RANGE(ptr, size);                                               \
  } while (0)
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(ptr, size)                         \
  do {                                                                         \
  } while (0)
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
  } while (0)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd)                                 \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd)                                 \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd)                    \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name)                          \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name)                 \
  do {                                                                         \
  } while (0)
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
#define COMMON_INTERCEPTOR_ON_EXIT(ctx) (0)

#include "sanitizer_common/sanitizer_common_interceptors.inc"
#include "sanitizer_common/sanitizer_platform_interceptors.h"
#include "sanitizer_common/sanitizer_signal_interceptors.inc"

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
  InitializeSignalInterceptors();

  INTERCEPT_FUNCTION(posix_memalign);
  INTERCEPT_FUNCTION(memalign);
  INTERCEPT_FUNCTION(aligned_alloc);
  INTERCEPT_FUNCTION(__libc_memalign);
  INTERCEPT_FUNCTION(valloc);
  INTERCEPT_FUNCTION(pvalloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(cfree);
  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(reallocarray);
  INTERCEPT_FUNCTION(mallopt);
  INTERCEPT_FUNCTION(malloc_stats);
  INTERCEPT_FUNCTION(mallinfo);
  INTERCEPT_FUNCTION(malloc_usable_size);

  INTERCEPT_FUNCTION(strdup);
  INTERCEPT_FUNCTION(__strdup);

  INTERCEPT_FUNCTION(pthread_create);
  INTERCEPT_FUNCTION(pthread_join);
  // INTERCEPT_FUNCTION(__cxa_atexit);

  inited = 1;
}

} // namespace __odef
