//===- OverflowDefense.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/OverflowDefense.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
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

const char kOdefModuleCtorName[] = "odef.module_ctor";
const char kOdefInitName[] = "__odef_init";

namespace {

enum AddrLoc {
  kAddrLocStack = 0,
  kAddrLocGlobal,
  kAddrLocAny,
  kAddrLocUnknown,
};

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
  bool NeverEscapedImpl(Instruction *I,
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
  void instrumentGepAndBc(Function &F);
  void instrumentBitCast(Function &F);
  void instrumentGep(Function &F);

  Value *getSource(Instruction *I);
  Value *getSourceImpl(Value *V);
  bool getPhiSource(Value *V, Value *&Src, SmallPtrSet<Value *, 16> &Visited);

  AddrLoc getLocation(Instruction *I);
  void getLocationImpl(Value *V, AddrLoc &Loc,
                       SmallPtrSet<Value *, 16> &Visited);

  bool Kernel;
  bool Recover;

  SmallVector<GetElementPtrInst *, 16> GepToInstrument;
  SmallVector<BitCastInst *, 16> BcToInstrument;

  SmallVector<GetElementPtrInst *, 16> SubFieldToInstrument;
  SmallVector<AllocaInst *, 16> AllocaToReplace;

  DenseMap<Value *, Value *> SourceCache;
  DenseMap<Value *, AddrLoc> LocationCache;

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

void insertModuleCtor(Module &M) {
  getOrCreateSanitizerCtorAndInitFunctions(
      M, kOdefModuleCtorName, kOdefInitName,
      /*InitArgTypes=*/{},
      /*InitArgs=*/{},
      // This callback is invoked when the functions are created the first
      // time. Hook them into the global ctors list in that case:
      [&](Function *Ctor, FunctionCallee) {
        appendToGlobalCtors(M, Ctor, 0, Ctor);
      });
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
  if (Options.Kernel)
    return PreservedAnalyses::all();
  insertModuleCtor(M);
  return PreservedAnalyses::none();
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
  // TODO: instrument subfield access do not require *any* runtime support, but
  // we still need to know how much they cost
  instrumentSubFieldAccess(F, SE);

  // Instrument GEP and BC
  // instrumentGepAndBc(F);

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
  return NeverEscapedImpl(I, Visited);
}

bool OverflowDefense::NeverEscapedImpl(
    Instruction *I, SmallPtrSet<Instruction *, 16> &Visited) {
  if (Visited.count(I))
    return true;

  Visited.insert(I);

  for (auto *U : I->users()) {
    if (auto *UI = dyn_cast<Instruction>(U)) {
      if (isEscapeInstruction(UI))
        return false;

      if (!NeverEscapedImpl(UI, Visited))
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
    Type *Ty = Gep->getPointerOperandType()->getPointerElementType();
    if (isFixedSizeType(Ty)) {
      bool isFirstField = true;
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

void OverflowDefense::dependencyOptimize(Function &F, DominatorTree &DT,
                                         PostDominatorTree &PDT,
                                         ScalarEvolution &SE) {
  SmallVector<BitCastInst *, 16> NewBcToInstrument =
      dependencyOptimizeForBc(F, DT, PDT, SE);
  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument =
      dependencyOptimizeForGep(F, DT, PDT, SE);

  // TODO: optimize for subfield access
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

Value *OverflowDefense::getSource(Instruction *I) {
  if (SourceCache.count(I))
    return SourceCache[I];

  return SourceCache[I] = getSourceImpl(I);
}

Value *OverflowDefense::getSourceImpl(Value *V) {
  if (SourceCache.count(V))
    return SourceCache[V];

  if (auto *BC = dyn_cast<BitCastInst>(V))
    return SourceCache[V] = getSourceImpl(BC->getOperand(0));

  if (auto *GEP = dyn_cast<GetElementPtrInst>(V))
    return SourceCache[V] = getSourceImpl(GEP->getPointerOperand());

  if (auto *GEPO = dyn_cast<GEPOperator>(V))
    return SourceCache[V] = getSourceImpl(GEPO->getPointerOperand());

  if (auto *BCO = dyn_cast<BitCastOperator>(V))
    return SourceCache[V] = getSourceImpl(BCO->getOperand(0));

  if (auto *Phi = dyn_cast<PHINode>(V)) {
    Value *Source = nullptr;
    SmallPtrSet<Value *, 16> Visited;
    if (getPhiSource(Phi, Source, Visited))
      return SourceCache[V] = Source;
    return SourceCache[V] = Phi;
  }

  return nullptr;
}

bool OverflowDefense::getPhiSource(Value *V, Value *&Src,
                                   SmallPtrSet<Value *, 16> &Visited) {
  if (Visited.count(V))
    return true;
  Visited.insert(V);
  if (PHINode *Phi = dyn_cast<PHINode>(V)) {
    for (size_t i = 0; i < Phi->getNumIncomingValues(); ++i) {
      if (!getPhiSource(Phi->getIncomingValue(i), Src, Visited))
        return false;
    }
    return true;
  }
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(V)) {
    return getPhiSource(GEP->getPointerOperand(), Src, Visited);
  }
  if (BitCastInst *BC = dyn_cast<BitCastInst>(V)) {
    return getPhiSource(BC->getOperand(0), Src, Visited);
  }
  if (GEPOperator *GEPO = dyn_cast<GEPOperator>(V)) {
    return getPhiSource(GEPO->getPointerOperand(), Src, Visited);
  }

  if (Src == nullptr) {
    Src = V;
    return true;
  }
  return Src == V;
}

AddrLoc OverflowDefense::getLocation(Instruction *I) {
  if (LocationCache.count(I))
    return LocationCache[I];

  AddrLoc AL = AddrLoc::kAddrLocUnknown;
  SmallPtrSet<Value *, 16> Visited;
  getLocationImpl(I, AL, Visited);

  assert(AL != AddrLoc::kAddrLocUnknown);
  return LocationCache[I] = AL;
}

void OverflowDefense::getLocationImpl(Value *V, AddrLoc &AL,
                                      SmallPtrSet<Value *, 16> &Visited) {

  Value *S = getSourceImpl(V);
  if (Visited.count(S))
    return;
  Visited.insert(S);

  AddrLoc _AL = AddrLoc::kAddrLocUnknown;

  if (isa<GlobalVariable>(S))
    _AL = AddrLoc::kAddrLocGlobal;
  else if (isa<AllocaInst>(S))
    _AL = AddrLoc::kAddrLocStack;
  else if (auto Phi = dyn_cast<PHINode>(S)) {
    for (size_t i = 0; i < Phi->getNumIncomingValues(); ++i) {
      getLocationImpl(Phi->getIncomingValue(i), _AL, Visited);
      if (_AL == kAddrLocAny)
        break;
    }
  } else {
    // TODO: Sometimes S is Constant, which is not handled here.
    _AL = AddrLoc::kAddrLocAny;
  }

  if (AL == AddrLoc::kAddrLocUnknown)
    AL = _AL;
  else if (AL != _AL)
    AL = AddrLoc::kAddrLocAny;
}

void OverflowDefense::instrumentGepAndBc(Function &F) {
  DenseMap<Value *, SmallVector<Instruction *, 16>> SourceMap;

  for (auto &I : GepToInstrument) {
    Value *Source = getSource(I);
    SourceMap[Source].push_back(I);
  }

  for (auto &I : BcToInstrument) {
    Value *Source = getSource(I);
    SourceMap[Source].push_back(I);
  }
}

void OverflowDefense::instrumentBitCast(Function &F) {
  // Instrument bitcast
  // ShadowAddr = Shadow(BC);
  // BackSize = *(int32_t *)ShadowAddr;
  // if (BackSize < TypeSze(BC))
  //   report_overflow();

  for (auto &I : BcToInstrument) {
    // TODO: instrument bitcast
  }
}

void OverflowDefense::instrumentGep(Function &F) {
  // Instrument gep
  // ShadowAddr = Shadow(GEP);
  // Packed = *(int32_t *)ShadowAddr;
  // FrontSize = Packed & 0xffffffff;
  // BackSize = Packed >> 32;
  // ChunkStart = Addr - FrontSize;
  // ChunkEnd = Addr + BackSize;
  // if (ChunkStart < Addr && Addr < ChunkEnd)
  //  report_overflow();

  // TODO: instrument gep
}