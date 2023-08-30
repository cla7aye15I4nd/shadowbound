//===- Transform/Instrumentation/OverflowDefense.h - Overflow Defense -----===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_OVERFLOWDEFENSE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_OVERFLOWDEFENSE_H

#include "llvm/IR/PassManager.h"
#include <string>

namespace llvm {

struct OverflowDefenseOptions {
  OverflowDefenseOptions() : OverflowDefenseOptions(false, false, "default"){};
  OverflowDefenseOptions(bool Kernel, bool Recover, std::string Runtime);
  bool Kernel;
  bool Recover;
  std::string Runtime;
};

struct OverflowDefensePass : public PassInfoMixin<OverflowDefensePass> {
  OverflowDefensePass(OverflowDefenseOptions Options) : Options(Options) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  OverflowDefenseOptions Options;
};

struct ModuleOverflowDefensePass
    : public PassInfoMixin<ModuleOverflowDefensePass> {
  ModuleOverflowDefensePass(OverflowDefenseOptions Options)
      : Options(Options) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  OverflowDefenseOptions Options;
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_OVERFLOWDEFENSE_H