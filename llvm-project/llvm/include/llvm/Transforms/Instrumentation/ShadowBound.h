//===- Transform/Instrumentation/ShadowBound.h - Overflow Defense -----===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_SHADOWBOUND_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_SHADOWBOUND_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/PassManager.h"
#include <set>
#include <string>

namespace llvm {

class Argument;
class Function;
class Value;

struct ShadowBoundOptions {
  ShadowBoundOptions() : ShadowBoundOptions(false){};
  explicit ShadowBoundOptions(bool Recover, bool LTOPostLink = false);
  bool Recover;
  // Set when the passes run inside the link-time pipeline on a merged module:
  // only functions marked by ShadowBoundLTOPrepPass are instrumented, and the
  // module is left untouched unless some input was compiled with ShadowBound.
  bool LTOPostLink;
};

/// Name of the module flag / function attribute that the LTO pre-link step
/// leaves behind for the link-time instrumentation.
extern const char kShadowBoundLTOMarker[];

/// Whole-module "never a heap pointer" facts, used to drop bounds checks whose
/// source can never point into the protected heap.
///
/// Only heap objects carry bounds metadata; a check on a pointer that is
/// derived from a stack or global object is skipped at run time anyway. This
/// analysis proves that statically, interprocedurally:
///   * an argument of a function whose every caller is visible (local linkage,
///     address never taken) is non-heap if every call site passes a non-heap
///     value;
///   * the return value of a function with an exact definition is non-heap if
///     every returned value is non-heap.
/// The two facts feed each other, so they are computed as a greatest fixpoint.
/// Under full LTO, internalization gives almost every function local linkage,
/// which is what makes this whole-program; without LTO it still covers static
/// functions soundly.
struct ShadowBoundIPOInfo {
  SmallPtrSet<const Argument *, 32> NonHeapArgs;
  SmallPtrSet<const Function *, 32> NonHeapReturns;

  /// Struct fields (named struct, field index) that some allocation sized by a
  /// single loaded value is stored into. Only filled with
  /// -shadowbound-struct-heuristic: it is the "struct" pattern of the old
  /// out-of-tree analyzer, which assumes accesses through such a field are
  /// guarded by the length it was allocated with. A heuristic, not a proof.
  std::set<std::pair<std::string, unsigned>> TrustedStructFields;

  /// True if \p V can never point into the heap.
  bool isNonHeap(const Value *V) const;

  /// True if \p Src is loaded from a field in TrustedStructFields.
  bool isTrustedStructField(Function &F, Value *Src) const;

  /// Function passes read this result through the outer-analysis proxy, which
  /// requires that it survive their invalidation. The facts are about run-time
  /// values, which instrumentation and cleanup preserve. It goes away only
  /// when explicitly abandoned, as addShadowBoundPasses does once the function
  /// passes are done.
  bool invalidate(Module &, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &);
};

class ShadowBoundIPOAnalysis
    : public AnalysisInfoMixin<ShadowBoundIPOAnalysis> {
  friend AnalysisInfoMixin<ShadowBoundIPOAnalysis>;
  static AnalysisKey Key;

public:
  using Result = ShadowBoundIPOInfo;
  Result run(Module &M, ModuleAnalysisManager &AM);
};

struct ShadowBoundPass : public PassInfoMixin<ShadowBoundPass> {
  ShadowBoundPass(ShadowBoundOptions Options) : Options(Options) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  ShadowBoundOptions Options;
};

struct ModuleShadowBoundPass
    : public PassInfoMixin<ModuleShadowBoundPass> {
  ModuleShadowBoundPass(ShadowBoundOptions Options)
      : Options(Options) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  ShadowBoundOptions Options;
};

/// LTO pre-link step: instead of instrumenting each translation unit, record
/// that this module (and each of its functions) asked for ShadowBound, so the
/// link-time pipeline can instrument the merged module with whole-program
/// knowledge.
struct ShadowBoundLTOPrepPass : public PassInfoMixin<ShadowBoundLTOPrepPass> {
  ShadowBoundLTOPrepPass(ShadowBoundOptions Options) : Options(Options) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  ShadowBoundOptions Options;
};

/// Link-time entry point, added to the (Thin)LTO default pipelines. A no-op
/// unless some input module was prepared by ShadowBoundLTOPrepPass; otherwise
/// it runs the same instrumentation pipeline clang uses for a normal compile,
/// on the merged module.
struct ShadowBoundLTOPass : public PassInfoMixin<ShadowBoundLTOPass> {
  ShadowBoundLTOPass(bool Optimize) : Optimize(Optimize) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  bool Optimize;
};

/// Adds the ShadowBound instrumentation pipeline to \p MPM: the module pass,
/// the interprocedural non-heap analysis, and the function pass followed (when
/// \p Optimize) by the cleanup passes the checks benefit from. Used both by
/// clang for a regular compile and by ShadowBoundLTOPass at link time.
/// Defined in the Passes library, which owns the cleanup passes.
void addShadowBoundPasses(ModulePassManager &MPM, ShadowBoundOptions Options,
                          bool Optimize);

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_SHADOWBOUND_H
