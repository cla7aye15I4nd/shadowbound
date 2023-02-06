//===- OverflowDefense.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/OverflowDefense.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include <algorithm>

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
  void dependencyOptimize(Function &F, DominatorTree &DT,
                          PostDominatorTree &PDT, ScalarEvolution &SE);

  SmallVector<BitCastInst *, 16> dependencyOptimizeForBc(Function &F,
                                                         DominatorTree &DT,
                                                         PostDominatorTree &PDT,
                                                         ScalarEvolution &SE);

  SmallVector<GetElementPtrInst *, 16>
  dependencyOptimizeForGep(Function &F, DominatorTree &DT,
                           PostDominatorTree &PDT, ScalarEvolution &SE);
  void instrumentSubFieldAccess(Function &F, ScalarEvolution &SE);

  bool Kernel;
  bool Recover;

  SmallVector<GetElementPtrInst *, 16> GepToInstrument;
  SmallVector<BitCastInst *, 16> BcToInstrument;

  SmallVector<GetElementPtrInst *, 16> SubFieldToInstrument;

  const DataLayout *DL;

  Type *int64Type;
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

bool isFixedSizeType(Type *Ty) {
  if (isUnionType(Ty))
    return false;

  if (Ty->isArrayTy())
    return true;

  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    if (ArrayType *Aty = dyn_cast<ArrayType>(STy->elements().back()))
      return Aty->getNumElements() != 0;
    return true;
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

void OverflowDefense::initializeModule(Module &M) {
  DL = &M.getDataLayout();
  int64Type = Type::getInt64Ty(M.getContext());
}

bool OverflowDefense::sanitizeFunction(Function &F,
                                       FunctionAnalysisManager &AM) {
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);

  // Collect all instructions to instrument
  collectToInstrument(F);

  dependencyOptimize(F, DT, PDT, SE);

  // Instrument subfield access
  instrumentSubFieldAccess(F, SE);

  return false;
}

void OverflowDefense::collectToInstrument(Function &F) {
  // TODO: collect glibc function call
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *Gep = dyn_cast<GetElementPtrInst>(&I)) {
        if (!filterToInstrument(F, Gep)) {
          GepToInstrument.push_back(Gep);
          SubFieldToInstrument.push_back(Gep);
        }
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
  for (auto *Gep : SubFieldToInstrument) {
    Type *Ty = Gep->getSourceElementType()->getPointerElementType();
    if (isFixedSizeType(Ty)) {
      bool isFirstField = true;
      for (auto &Op : Gep->indices()) {
        if (isFirstField && Ty->isStructTy()) {
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

void OverflowDefense::dependencyOptimize(Function &F, DominatorTree &DT,
                                         PostDominatorTree &PDT,
                                         ScalarEvolution &SE) {
  SmallVector<BitCastInst *, 16> NewBcToInstrument =
      dependencyOptimizeForBc(F, DT, PDT, SE);
  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument =
      dependencyOptimizeForGep(F, DT, PDT, SE);

  BcToInstrument.swap(NewBcToInstrument);
  GepToInstrument.swap(NewGepToInstrument);
}

SmallVector<BitCastInst *, 16>
OverflowDefense::dependencyOptimizeForBc(Function &F, DominatorTree &DT,
                                         PostDominatorTree &PDT,
                                         ScalarEvolution &SE) {
  SmallVector<BitCastInst *, 16> NewBcToInstrument;

  for (size_t i = 0; i < BcToInstrument.size(); ++i) {
    bool optimized = false;
    for (size_t j = 0; j < BcToInstrument.size(); ++j) {
      if (i != j) {
        auto I = BcToInstrument[i];
        auto J = BcToInstrument[j];
        if (DT.dominates(J, I) || PDT.dominates(J, I)) {
          if (I->getOperand(0) == J->getOperand(0)) {
            size_t ISize =
                DL->getTypeStoreSize(I->getType()->getPointerElementType());
            size_t JSize =
                DL->getTypeStoreSize(J->getType()->getPointerElementType());
            if (ISize <= JSize) {
              optimized = true;
              break;
            }
          }
        }
      }
    }

    if (!optimized) {
      NewBcToInstrument.push_back(BcToInstrument[i]);
    }
  }

  return NewBcToInstrument;
}

SmallVector<GetElementPtrInst *, 16>
OverflowDefense::dependencyOptimizeForGep(Function &F, DominatorTree &DT,
                                          PostDominatorTree &PDT,
                                          ScalarEvolution &SE) {
  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument;

  for (size_t i = 0; i < GepToInstrument.size(); ++i) {
    bool optimized = false;
    auto I = GepToInstrument[i];

    if (I->getPointerOperand()
            ->getType()
            ->getPointerElementType()
            ->isArrayTy()) {
      // Arraytype pointer's overflow will be detected subfield access checking
      optimized = true;
    } else {
      for (size_t j = 0; j < GepToInstrument.size(); ++j) {
        if (i != j) {
          auto J = GepToInstrument[j];
          if (DT.dominates(J, I) || PDT.dominates(J, I)) {

            // The pointer operand of I and J are the same
            if (I->getPointerOperand() == J->getPointerOperand()) {
              Type *ty =
                  I->getPointerOperand()->getType()->getPointerElementType();

              size_t numIndex =
                  isFixedSizeType(ty)
                      ? 1
                      : std::max(I->getNumIndices(), J->getNumIndices());
              bool NotGreater = true;

              // Compare the offset of each index if every offset of I is always
              // smaller than J, then I is not need to be instrumented
              for (size_t k = 0; k < numIndex; ++k) {
                auto IOffset = k >= I->getNumIndices()
                                   ? ConstantInt::getNullValue(int64Type)
                                   : I->getOperand(k + 1);
                auto JOffset = k >= J->getNumIndices()
                                   ? ConstantInt::getNullValue(int64Type)
                                   : J->getOperand(k + 1);

                // If the max offset of I is larger than the min offset of J,
                // then it is possible that the offset of I is greater than the
                // offset of J at runtime.
                if (SE.getUnsignedRangeMax(SE.getSCEV(IOffset)).getZExtValue() >
                    SE.getUnsignedRangeMin(SE.getSCEV(JOffset))
                        .getZExtValue()) {
                  NotGreater = false;
                  break;
                }
              }
              if (NotGreater) {
                optimized = true;
                break;
              }
            }
          }
        }
      }
    }

    if (!optimized)
      NewGepToInstrument.push_back(GepToInstrument[i]);
  }

  return NewGepToInstrument;
}