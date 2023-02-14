#include "odef_thread.h"
#include "odef.h"

#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __odef {

OdefThread *OdefThread::Create(thread_callback_t start_routine, void *arg) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(OdefThread), PageSize);
  OdefThread *thread = (OdefThread *)MmapOrDie(size, __func__);
  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  thread->destructor_iterations_ = GetPthreadDestructorIterations();

  return thread;
}

void OdefThread::SetThreadStackAndTls() {
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(IsMainThread(), &stack_.bottom, &stack_size, &tls_begin_,
                       &tls_size);
  stack_.top = stack_.bottom + stack_size;
  tls_end_ = tls_begin_ + tls_size;

  int local;
  CHECK(AddrIsInStack((uptr)&local));
}

void OdefThread::Init() {
  SetThreadStackAndTls();
  CHECK(MEM_IS_APP(stack_.bottom));
  CHECK(MEM_IS_APP(stack_.top - 1));
  // ClearShadowForThreadStackAndTLS();
}

void OdefThread::TSDDtor(void *tsd) {
  OdefThread *t = (OdefThread *)tsd;
  t->Destroy();
}

void OdefThread::Destroy() {
  malloc_storage().CommitBack();
  // We also clear the shadow on thread destruction because
  // some code may still be executing in later TSD destructors
  // and we don't want it to have any poisoned stack.
  // ClearShadowForThreadStackAndTLS();
  uptr size = RoundUpTo(sizeof(OdefThread), GetPageSizeCached());
  UnmapOrDie(this, size);
  DTLS_Destroy();
}

thread_return_t OdefThread::ThreadStart() {
  if (!start_routine_) {
    // start_routine_ == 0 if we're on the main thread or on one of the
    // OS X libdispatch worker threads. But nobody is supposed to call
    // ThreadStart() for the worker threads.
    return 0;
  }

  thread_return_t res = start_routine_(arg_);

  return res;
}

OdefThread::StackBounds OdefThread::GetStackBounds() const {
  if (!stack_switching_)
    return {stack_.bottom, stack_.top};
  const uptr cur_stack = GET_CURRENT_FRAME();
  // Note: need to check next stack first, because FinishSwitchFiber
  // may be in process of overwriting stack_.top/bottom_. But in such case
  // we are already on the next stack.
  if (cur_stack >= next_stack_.bottom && cur_stack < next_stack_.top)
    return {next_stack_.bottom, next_stack_.top};
  return {stack_.bottom, stack_.top};
}

uptr OdefThread::stack_top() { return GetStackBounds().top; }

uptr OdefThread::stack_bottom() { return GetStackBounds().bottom; }

bool OdefThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

void OdefThread::StartSwitchFiber(uptr bottom, uptr size) {
  CHECK(!stack_switching_);
  next_stack_.bottom = bottom;
  next_stack_.top = bottom + size;
  stack_switching_ = true;
}

void OdefThread::FinishSwitchFiber(uptr *bottom_old, uptr *size_old) {
  CHECK(stack_switching_);
  if (bottom_old)
    *bottom_old = stack_.bottom;
  if (size_old)
    *size_old = stack_.top - stack_.bottom;
  stack_.bottom = next_stack_.bottom;
  stack_.top = next_stack_.top;
  stack_switching_ = false;
  next_stack_.top = 0;
  next_stack_.bottom = 0;
}

} // namespace __odef