#ifndef SHADOWBOUND_THREAD_H
#define SHADOWBOUND_THREAD_H

#include "shadowbound_allocator.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_posix.h"

namespace __shadowbound {

class ShadowBoundThread {
public:
  static ShadowBoundThread *Create(thread_callback_t start_routine, void *arg);
  static void TSDDtor(void *tsd);
  void Destroy();

  void Init(); // Should be called from the thread itself.
  thread_return_t ThreadStart();

  uptr stack_top();
  uptr stack_bottom();
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  bool IsMainThread() { return start_routine_ == nullptr; }

  bool AddrIsInStack(uptr addr);

  void StartSwitchFiber(uptr bottom, uptr size);
  void FinishSwitchFiber(uptr *bottom_old, uptr *size_old);

  ShadowBoundThreadLocalMallocStorage &malloc_storage() { return malloc_storage_; }

  int destructor_iterations_;
  __sanitizer_sigset_t starting_sigset_;

private:
  // NOTE: There is no ShadowBoundThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.
  void SetThreadStackAndTls();
  struct StackBounds {
    uptr bottom;
    uptr top;
  };
  StackBounds GetStackBounds() const;
  thread_callback_t start_routine_;
  void *arg_;

  bool stack_switching_;

  StackBounds stack_;
  StackBounds next_stack_;

  uptr tls_begin_;
  uptr tls_end_;

  ShadowBoundThreadLocalMallocStorage malloc_storage_;
};

ShadowBoundThread *GetCurrentThread();
void SetCurrentThread(ShadowBoundThread *t);

} // namespace __shadowbound

#endif // SHADOWBOUND_THREAD_H