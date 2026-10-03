//===- Transform/Instrumentation/ShadowBound.h - Overflow Defense -----===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_SHADOWBOUND_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_SHADOWBOUND_H

#include "llvm/IR/PassManager.h"
#include <string>

namespace llvm {

struct ShadowBoundOptions {
  ShadowBoundOptions() : ShadowBoundOptions(false){};
  explicit ShadowBoundOptions(bool Recover);
  bool Recover;
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
} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_SHADOWBOUND_H