//===- OverflowDefense.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/OverflowDefense.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "odef"

static const int kReservedBytes = 8;

static cl::opt<bool>
    ClEnableKodef("odef-kernel",
                  cl::desc("Enable KernelOverflowDefense instrumentation"),
                  cl::Hidden, cl::init(false));

static cl::opt<bool> ClKeepGoing("odef-keep-going",
                                 cl::desc("keep going after reporting a error"),
                                 cl::Hidden, cl::init(false));

namespace {

class OverflowDefense {
public:
  OverflowDefense(Module &M, const OverflowDefenseOptions &Options)
      : Kernel(Options.Kernel), Recover(Options.Recover) {
    initializeModule(M);
  }

  OverflowDefense(const OverflowDefense &&) = delete;
  OverflowDefense &operator=(const OverflowDefense &&) = delete;
  OverflowDefense(const OverflowDefense &) = delete;
  OverflowDefense &operator=(const OverflowDefense &) = delete;

  bool sanitizeFunction(Function &F, FunctionAnalysisManager &FAM);

private:
  void initializeModule(Module &M);
  void collectToInstrument(Function &F);

  bool filterToInstrument(Function &F, Instruction *I);
  bool NeverEscaped(Instruction *I);
  bool NeverEscapedInternal(Instruction *I,
                            SmallPtrSet<Instruction *, 16> &Visited);
  bool isShrinkBitCast(Instruction *I);
  bool isSafeFieldAccess(Instruction *I);

  void instrumentSubFieldAccess(Function &F, ScalarEvolution &SE);

  bool Kernel;
  bool Recover;

  SmallVector<GetElementPtrInst *, 16> GepToInstrument;
  SmallVector<BitCastInst *, 16> BcToInstrument;

  const DataLayout *DL;
};

bool isEscapeInstruction(Instruction *I) {
  // TODO: Add uncommon escape instructions
  if (isa<StoreInst>(I) || isa<LoadInst>(I) || isa<ReturnInst>(I))
    return true;

  static SmallVector<StringRef, 16> whitelist = {"llvm.prefetch."};

  if (auto *CI = dyn_cast<CallInst>(I)) {
    Function *F = CI->getCalledFunction();
    if (F != nullptr) {
      for (auto &name : whitelist) {
        if (F->getName().startswith(name))
          return false;
      }

      return true;
    }
  }

  return false;
}

bool isUnionType(Type *Ty) {
  if (auto *STy = dyn_cast<StructType>(Ty))
    return STy->hasName() && STy->getName().startswith("union.");

  return false;
}

bool isZeroAccessGep(Instruction *I) {
  if (auto *Gep = dyn_cast<GetElementPtrInst>(I)) {
    for (auto &Op : Gep->indices()) {
      if (auto *C = dyn_cast<ConstantInt>(&Op)) {
        if (0 != C->getSExtValue())
          return false;
      }
    }
  }

  return true;
}

bool isVirtualTableGep(Instruction *I) {
  // TODO: check the condition of virtual table access is correct
  if (auto *Gep = dyn_cast<GetElementPtrInst>(I)) {
    if (auto *pty = dyn_cast<PointerType>(
            Gep->getPointerOperand()->getType()->getPointerElementType())) {
      if (auto *fty = dyn_cast<FunctionType>(pty->getPointerElementType())) {
        if (fty->getNumParams() >= 1) {
          if (auto *fpty = dyn_cast<PointerType>(fty->getParamType(0))) {
            if (fpty->getPointerElementType()->isStructTy()) {
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  return false;
}

template <class T> T getOptOrDefault(const cl::opt<T> &Opt, T Default) {
  return (Opt.getNumOccurrences() > 0) ? Opt : Default;
}
} // end anonymous namespace

OverflowDefenseOptions::OverflowDefenseOptions(bool Kernel, bool Recover)
    : Kernel(getOptOrDefault(ClEnableKodef, Kernel)),
      Recover(getOptOrDefault(ClKeepGoing, Kernel || Recover)) {}

PreservedAnalyses OverflowDefensePass::run(Function &F,
                                           FunctionAnalysisManager &FAM) {
  OverflowDefense Odef(*F.getParent(), Options);
  if (Odef.sanitizeFunction(F, FAM))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

PreservedAnalyses ModuleOverflowDefensePass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  return PreservedAnalyses::all();
}

void OverflowDefense::initializeModule(Module &M) { DL = &M.getDataLayout(); }

bool OverflowDefense::sanitizeFunction(Function &F, FunctionAnalysisManager &AM) {
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);

  // Collect all instructions to instrument
  collectToInstrument(F);

  // Instrument subfield access
  instrumentSubFieldAccess(F, SE);

  return false;
}

void OverflowDefense::collectToInstrument(Function &F) {
  // TODO: collect glibc function call
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *Gep = dyn_cast<GetElementPtrInst>(&I)) {
        if (!filterToInstrument(F, Gep))
          GepToInstrument.push_back(Gep);
      } else if (auto *Bc = dyn_cast<BitCastInst>(&I)) {
        if (!filterToInstrument(F, Bc))
          BcToInstrument.push_back(Bc);
      }
    }
  }
}

bool OverflowDefense::filterToInstrument(Function &F, Instruction *I) {
  assert(I->getType()->isPointerTy());

  if (NeverEscaped(I))
    return true;

  if (isShrinkBitCast(I))
    return true;

  if (isZeroAccessGep(I))
    return true;

  if (isVirtualTableGep(I))
    return true;

  return false;
}

bool OverflowDefense::NeverEscaped(Instruction *I) {
  SmallPtrSet<Instruction *, 16> Visited;
  return NeverEscapedInternal(I, Visited);
}

bool OverflowDefense::NeverEscapedInternal(
    Instruction *I, SmallPtrSet<Instruction *, 16> &Visited) {
  if (Visited.count(I))
    return true;

  Visited.insert(I);

  for (auto *U : I->users()) {
    if (auto *UI = dyn_cast<Instruction>(U)) {
      if (isEscapeInstruction(UI))
        return false;

      if (!NeverEscapedInternal(UI, Visited))
        return false;
    } else {
      // TODO: handle non-instruction user
    }
  }

  return true;
}

bool OverflowDefense::isShrinkBitCast(Instruction *I) {
  if (auto *BC = dyn_cast<BitCastInst>(I)) {
    if (!BC->getSrcTy()->isPointerTy() || !BC->getDestTy()->isPointerTy())
      return false;

    Type *srcTy = BC->getSrcTy()->getPointerElementType();
    Type *dstTy = BC->getDestTy()->getPointerElementType();

    if (isUnionType(srcTy) || isUnionType(dstTy))
      return false;

    if (!srcTy->isSized() || !dstTy->isSized())
      return false;

    TypeSize srcSize = DL->getTypeStoreSize(srcTy);
    TypeSize dstSize = DL->getTypeStoreSize(dstTy);

    return dstSize <= srcSize;
  }

  return false;
}

void OverflowDefense::instrumentSubFieldAccess(Function &F,
                                               ScalarEvolution &SE) {
  for (auto *Gep : GepToInstrument) {
    if (StructType *STy = dyn_cast<StructType>(
            Gep->getSourceElementType()->getPointerElementType())) {
      if (!isUnionType(STy)) {
        bool isFirstField = true;
        Type *Ty = STy;
        for (auto &Op : Gep->indices()) {
          if (isFirstField) {
            isFirstField = false;
            continue;
          }

          auto value = Op.get();

          // determine the type of value is int32 or int64
          if (value->getType()->isIntegerTy(32)) {
            assert(Ty->isStructTy());
            assert(isa<ConstantInt>(value));
            assert(cast<ConstantInt>(value)->getZExtValue() <
                   STy->getNumElements());

            Ty = cast<StructType>(Ty)->getElementType(
                cast<ConstantInt>(value)->getZExtValue());
          } else {
            assert(value->getType()->isIntegerTy(64));
            assert(Ty->isArrayTy());

            auto Aty = cast<ArrayType>(Ty);
            if (SE.getUnsignedRangeMax(SE.getSCEV(value)).getZExtValue() >=
                Aty->getNumElements()) {
              // TODO: Instrument a subfield access checking
            }

            Ty = Aty->getArrayElementType();
          }
        }
      }
    }
  }
}