//===- OverflowDefense.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/OverflowDefense.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>

using namespace llvm;
using BuilderTy = IRBuilder<TargetFolder>;

#define DEBUG_TYPE "odef"

// Please use this macro instead of assert()
#define ASSERT(X)                                                              \
  do {                                                                         \
    if (!(X)) {                                                                \
      printf("Assertion failed: " #X "\n");                                    \
      abort();                                                                 \
    }                                                                          \
  } while (0)

static const int kReservedBytes = 8;

static const uint64_t kShadowBase = ~0x7ULL;
static const uint64_t kShadowMask = ~0x400000000007ULL;
static const uint64_t kAllocatorSpaceBegin = 0x600000000000ULL;
static const uint64_t kAllocatorStackBegin = 0x7ffffffde000ULL;
static const uint64_t kAllocatorSpaceEnd = 0x800000000000ULL;
static const uint64_t kMaxAddress = 0x1000000000000ULL;

static cl::opt<bool>
    ClEnableKodef("odef-kernel",
                  cl::desc("Enable KernelOverflowDefense instrumentation"),
                  cl::Hidden, cl::init(false));

static cl::opt<bool> ClKeepGoing("odef-keep-going",
                                 cl::desc("keep going after reporting a error"),
                                 cl::Hidden, cl::init(false));

static cl::opt<bool> ClSkipInstrument("odef-skip-instrument",
                                      cl::desc("skip instrumenting"),
                                      cl::Hidden, cl::init(false));

// Please note that due to limitations in the current implementation, we cannot
// guarantee that all corresponding checks will be disabled when the
// odef-check-[heap|stack|global] option is set to false. However, in most
// cases, the majority of these checks will be skipped.

static cl::opt<bool> ClCheckHeap("odef-check-heap",
                                 cl::desc("check heap memory"), cl::Hidden,
                                 cl::init(true));

static cl::opt<bool> ClCheckStack("odef-check-stack",
                                  cl::desc("check stack memory"), cl::Hidden,
                                  cl::init(true));

static cl::opt<bool> ClCheckGlobal("odef-check-global",
                                   cl::desc("check global memory"), cl::Hidden,
                                   cl::init(true));

static cl::opt<bool> ClCheckInField("odef-check-in-field",
                                    cl::desc("check in-field memory"),
                                    cl::Hidden, cl::init(true));

const char kOdefModuleCtorName[] = "odef.module_ctor";
const char kOdefInitName[] = "__odef_init";
const char kOdefReportName[] = "__odef_report";
const char kOdefAbortName[] = "__odef_abort";

namespace {

enum AddrLoc {
  kAddrLocStack = 0,
  kAddrLocGlobal,
  kAddrLocAny,
  kAddrLocUnknown,
};

enum CheckType {
  kRuntimeCheck = 0,
  kClusterCheck = 1,
  kBuiltInCheck = 2,
  kInFieldCheck = 3,
  kCheckTypeEnd
};

struct FieldCheck {
  GetElementPtrInst *Gep;
  SmallVector<std::pair<Value *, uint64_t>, 16> SubFields;

  FieldCheck(GetElementPtrInst *Gep,
             SmallVector<std::pair<Value *, uint64_t>, 16> SubFields)
      : Gep(Gep), SubFields(SubFields) {}
};

struct ChunkCheck {
  CheckType Type;
  Value *Src;
  SmallVector<Instruction *, 16> Insts;

  // Used for built-in checks
  Value *Size;
  Value *Offset;

  ChunkCheck(CheckType Type, Value *Src, SmallVector<Instruction *, 16> Insts)
      : Type(Type), Src(Src), Insts(Insts), Size(nullptr), Offset(nullptr) {}

  ChunkCheck(CheckType Type, Value *Src, SmallVector<Instruction *, 16> Insts,
             Value *Size, Value *Offset)
      : Type(Type), Src(Src), Insts(Insts), Size(Size), Offset(Offset) {}
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
  void collectToInstrument(Function &F, ObjectSizeOffsetEvaluator &ObjSizeEval,
                           ScalarEvolution &SE);
  void collectToReplace(Function &F);

  bool filterToInstrument(Function &F, Instruction *I,
                          ObjectSizeOffsetEvaluator &ObjSizeEval,
                          ScalarEvolution &SE);
  bool NeverEscaped(Instruction *I);
  bool NeverEscapedImpl(Instruction *I,
                        SmallPtrSet<Instruction *, 16> &Visited);
  bool isShrinkBitCast(Instruction *I);
  bool isSafePointer(Instruction *Ptr, ObjectSizeOffsetEvaluator &ObjSizeEval,
                     ScalarEvolution &SE);
  bool isSafeFieldAccess(Instruction *I);
  bool isAccessMember(Instruction *I);
  void dependencyOptimize(Function &F, DominatorTree &DT,
                          PostDominatorTree &PDT, ScalarEvolution &SE);
  bool isInterestingPointer(Function &F, Value *V);
  bool isInterestingPointerImpl(Function &F, Value *V,
                                SmallPtrSet<Value *, 16> &Visited);

  SmallVector<BitCastInst *, 16> dependencyOptimizeForBc(Function &F,
                                                         DominatorTree &DT,
                                                         PostDominatorTree &PDT,
                                                         ScalarEvolution &SE);

  SmallVector<GetElementPtrInst *, 16>
  dependencyOptimizeForGep(Function &F, DominatorTree &DT,
                           PostDominatorTree &PDT, ScalarEvolution &SE);
  void collectSubFieldCheck(Function &F, ScalarEvolution &SE);
  void collectChunkCheck(Function &F, LoopInfo &LI,
                         ObjectSizeOffsetEvaluator &ObjSizeEval);
  void collectChunkCheckImpl(Function &F, Value *Src,
                             SmallVector<Instruction *, 16> &Insts,
                             LoopInfo &LI,
                             ObjectSizeOffsetEvaluator &ObjSizeEval);
  void instrumentCluster(Function &F, Value *Src,
                         SmallVector<Instruction *, 16> &Insts);
  bool tryRuntimeFreeCheck(Function &F, Value *Src,
                           SmallVector<Instruction *, 16> &Insts,
                           ObjectSizeOffsetEvaluator &ObjSizeEval);

  void commitInstrument(Function &F);
  void commitFieldCheck(Function &F, FieldCheck &Check);
  void commitChunkCheck(Function &F, ChunkCheck &Check);

  void commitBuiltInCheck(Function &F, ChunkCheck &Check);
  void commitClusterCheck(Function &F, ChunkCheck &Check);
  void commitRuntimeCheck(Function &F, ChunkCheck &Check);

  void instrumentBitCast(Value *Src, BitCastInst *BC);
  void instrumentGep(Value *Src, GetElementPtrInst *GEP);
  void replaceAlloca(Function &F);

  Value *getSource(Instruction *I);
  Value *getSourceImpl(Value *V);
  bool getPhiSource(Value *V, Value *&Src, SmallPtrSet<Value *, 16> &Visited);

  AddrLoc getLocation(Instruction *I);
  void getLocationImpl(Value *V, AddrLoc &Loc,
                       SmallPtrSet<Value *, 16> &Visited);

  void CreateTrapBB(BuilderTy &B, Value *Cond, bool Abort);

  bool Kernel;
  bool Recover;

  SmallVector<GetElementPtrInst *, 16> GepToInstrument;
  SmallVector<BitCastInst *, 16> BcToInstrument;

  SmallVector<GetElementPtrInst *, 16> SubFieldToInstrument;
  SmallVector<AllocaInst *, 16> AllocaToReplace;

  DenseMap<Value *, Value *> SourceCache;
  DenseMap<Value *, AddrLoc> LocationCache;
  DenseMap<Value *, bool> InterestingPointerCache;

  SmallVector<ChunkCheck, 16> ChunkChecks;
  SmallVector<FieldCheck, 16> FieldChecks;

  const DataLayout *DL;

  int Counter[kCheckTypeEnd];

  Type *int32Type;
  Type *int64Type;
  Type *int32PtrType;
  Type *int64PtrType;

  Function *ReportFn;
  Function *AbortFn;
};

bool isEscapeInstruction(Instruction *I) {
  // TODO: Add uncommon escape instructions
  if (isa<StoreInst>(I) || isa<LoadInst>(I) || isa<ReturnInst>(I))
    return true;

  static SmallVector<StringRef, 16> whitelist = {"llvm.prefetch."};

  if (auto *CI = dyn_cast<CallInst>(I)) {
    Function *F = CI->getCalledFunction();
    if (F == nullptr)
      return true;

    for (auto &name : whitelist) {
      if (F->getName().startswith(name))
        return false;
    }

    return true;
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
      } else {
        return false;
      }
    }

    return true;
  }

  return false;
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
    if (ArrayType *Aty = dyn_cast<ArrayType>(STy->elements().back())) {
      // Avoid Check Some Flexible Array Member
      // struct page_entry {
      //    ...
      //    unsigned long in_use_p[1];
      // } page_entry;
      return Aty->getNumElements() > 1;
    }
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

void insertRuntimeFunction(Module &M) {
  LLVMContext &C = M.getContext();
  M.getOrInsertFunction(kOdefReportName, Type::getVoidTy(C));
  M.getOrInsertFunction(kOdefAbortName, Type::getVoidTy(C));
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
  insertRuntimeFunction(M);
  return PreservedAnalyses::none();
}

void OverflowDefense::initializeModule(Module &M) {
  DL = &M.getDataLayout();
  ReportFn = M.getFunction(kOdefReportName);
  AbortFn = M.getFunction(kOdefAbortName);

  int32Type = Type::getInt32Ty(M.getContext());
  int64Type = Type::getInt64Ty(M.getContext());
  int32PtrType = Type::getInt32PtrTy(M.getContext());
  int64PtrType = Type::getInt64PtrTy(M.getContext());

  memset(Counter, 0, sizeof(Counter));
}

bool OverflowDefense::sanitizeFunction(Function &F,
                                       FunctionAnalysisManager &AM) {
  if (F.isIntrinsic())
    return false;

  if (F.getInstructionCount() == 0)
    return false;

  if (F.getName() == kOdefInitName || F.getName() == kOdefModuleCtorName)
    return false;

  if (ClSkipInstrument)
    return false;

  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);

  ObjectSizeOpts EvalOpts;
  EvalOpts.RoundToAlign = true;
  ObjectSizeOffsetEvaluator ObjSizeEval(*DL, &TLI, F.getContext(), EvalOpts);

  // Collect all instructions to instrument
  collectToInstrument(F, ObjSizeEval, SE);
  collectToReplace(F);

  dependencyOptimize(F, DT, PDT, SE);

  // Instrument subfield access
  // TODO: instrument subfield access do not require *any* runtime support, but
  // we still need to know how much they cost
  collectSubFieldCheck(F, SE);

  // Instrument GEP and BC
  collectChunkCheck(F, LI, ObjSizeEval);

  commitInstrument(F);

  dbgs() << "[" << F.getName() << "]\n"
         << "  Builtin Check: " << Counter[kBuiltInCheck] << "\n"
         << "  Cluster Check: " << Counter[kClusterCheck] << "\n"
         << "  Runtime Check: " << Counter[kRuntimeCheck] << "\n"
         << "  InField Check: " << Counter[kInFieldCheck] << "\n";

  return true;
}

void OverflowDefense::collectToInstrument(
    Function &F, ObjectSizeOffsetEvaluator &ObjSizeEval, ScalarEvolution &SE) {
  // TODO: collect glibc function call
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *Gep = dyn_cast<GetElementPtrInst>(&I)) {
        if (!filterToInstrument(F, Gep, ObjSizeEval, SE)) {
          GepToInstrument.push_back(Gep);
          SubFieldToInstrument.push_back(Gep);
        }
      } else if (auto *Bc = dyn_cast<BitCastInst>(&I)) {
        if (!filterToInstrument(F, Bc, ObjSizeEval, SE))
          BcToInstrument.push_back(Bc);
      }
    }
  }
}

void OverflowDefense::collectToReplace(Function &F) {
  // Check all Alloca instructions

  for (auto &BB : F)
    for (auto &I : BB)
      if (auto *Alloca = dyn_cast<AllocaInst>(&I))
        if (isInterestingPointer(F, Alloca))
          AllocaToReplace.push_back(Alloca);
}

bool OverflowDefense::isInterestingPointer(Function &F, Value *V) {
  if (InterestingPointerCache.count(V))
    return InterestingPointerCache[V];

  SmallPtrSet<Value *, 16> Visited;
  return InterestingPointerCache[V] = isInterestingPointerImpl(F, V, Visited);
}

bool OverflowDefense::isInterestingPointerImpl(
    Function &F, Value *V, SmallPtrSet<Value *, 16> &Visited) {
  if (Visited.count(V))
    return false;

  Visited.insert(V);

  for (auto *U : V->users()) {
    if (isa<GetElementPtrInst>(U) || isa<BitCastInst>(U) || isa<PHINode>(U) ||
        isa<SelectInst>(U))
      return isInterestingPointerImpl(F, U, Visited);

    if (auto *SI = dyn_cast<StoreInst>(U))
      return SI->getValueOperand() == V;

    if (isa<ReturnInst>(U))
      return true;

    if (auto *CI = dyn_cast<CallBase>(U)) {
      Function *F = CI->getCalledFunction();
      return F == nullptr || !F->isIntrinsic();
    }

    if (isa<ICmpInst>(U) || isa<LoadInst>(U))
      return false;

    // TODO: handle more cases
    if (isa<PtrToIntInst>(U)) {
      // FIXME: PtrToIntInst is not handled yet
      return false;
    }

    errs() << "[Unhandled Instruction] " << *U << "\n";
    __builtin_unreachable();
  }

  return false;
}

bool OverflowDefense::filterToInstrument(Function &F, Instruction *I,
                                         ObjectSizeOffsetEvaluator &ObjSizeEval,
                                         ScalarEvolution &SE) {
  if (!I->getType()->isPointerTy())
    return true;

  if (NeverEscaped(I))
    return true;

  if (isShrinkBitCast(I))
    return true;

  if (isZeroAccessGep(I))
    return true;

  if (isVirtualTableGep(I))
    return true;

  if (isSafePointer(I, ObjSizeEval, SE))
    return true;

  return false;
}

bool OverflowDefense::isSafePointer(Instruction *Ptr,
                                    ObjectSizeOffsetEvaluator &ObjSizeEval,
                                    ScalarEvolution &SE) {
  SizeOffsetEvalType SizeOffsetEval = ObjSizeEval.compute(Ptr);

  if (!ObjSizeEval.bothKnown(SizeOffsetEval))
    return false;

  Value *Size = SizeOffsetEval.first;
  Value *Offset = SizeOffsetEval.second;

  BuilderTy IRB(Ptr->getParent(), Ptr->getIterator(), TargetFolder(*DL));
  ConstantInt *SizeCI = dyn_cast<ConstantInt>(Size);

  Type *IntTy = DL->getIntPtrType(Ptr->getType());
  uint32_t NeededSize = DL->getTypeStoreSize(Ptr->getType());
  Value *NeededSizeVal = ConstantInt::get(IntTy, NeededSize);

  auto SizeRange = SE.getUnsignedRange(SE.getSCEV(Size));
  auto OffsetRange = SE.getUnsignedRange(SE.getSCEV(Offset));
  auto NeededSizeRange = SE.getUnsignedRange(SE.getSCEV(NeededSizeVal));

  // three checks are required to ensure safety:
  // . Offset >= 0  (since the offset is given from the base ptr)
  // . Size >= Offset  (unsigned)
  // . Size - Offset >= NeededSize  (unsigned)
  //
  // optimization: if Size >= 0 (signed), skip 1st check
  Value *ObjSize = IRB.CreateSub(Size, Offset);
  Value *Cmp2 = SizeRange.getUnsignedMin().uge(OffsetRange.getUnsignedMax())
                    ? ConstantInt::getFalse(Ptr->getContext())
                    : IRB.CreateICmpULT(Size, Offset);
  Value *Cmp3 = SizeRange.sub(OffsetRange)
                        .getUnsignedMin()
                        .uge(NeededSizeRange.getUnsignedMax())
                    ? ConstantInt::getFalse(Ptr->getContext())
                    : IRB.CreateICmpULT(ObjSize, NeededSizeVal);
  Value *Or = IRB.CreateOr(Cmp2, Cmp3);
  if ((!SizeCI || SizeCI->getValue().slt(0)) &&
      !SizeRange.getSignedMin().isNonNegative()) {
    Value *Cmp1 = IRB.CreateICmpSLT(Offset, ConstantInt::get(IntTy, 0));
    Or = IRB.CreateOr(Cmp1, Or);
  }

  ConstantInt *C = dyn_cast_or_null<ConstantInt>(Or);
  return C && !C->getZExtValue();
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

void OverflowDefense::collectSubFieldCheck(Function &F, ScalarEvolution &SE) {
  for (auto *Gep : SubFieldToInstrument) {
    Type *Ty = Gep->getPointerOperandType()->getPointerElementType();

    if (isa<GlobalVariable>(Gep->getPointerOperand())) {
      // TODO: how to handle global variable's size?
      continue;
    }

    if (isFixedSizeType(Ty)) {
      bool isFirstField = true;
      SmallVector<std::pair<Value *, uint64_t>, 16> SubFields;

      for (auto &Op : Gep->indices()) {
        if (isFirstField) {
          isFirstField = false;
          continue;
        }

        auto value = Op.get();

        // determine the type of value is int32 or int64
        if (value->getType()->isIntegerTy(32)) {
          ASSERT(Ty->isStructTy());
          ASSERT(isa<ConstantInt>(value));
          ASSERT(cast<ConstantInt>(value)->getZExtValue() <
                 cast<StructType>(Ty)->getNumElements());

          Ty = cast<StructType>(Ty)->getElementType(
              cast<ConstantInt>(value)->getZExtValue());
        } else {
          ASSERT(value->getType()->isIntegerTy(64));
          ASSERT(Ty->isArrayTy());

          auto Aty = cast<ArrayType>(Ty);
          if (SE.getUnsignedRangeMax(SE.getSCEV(value)).getZExtValue() >=
              Aty->getNumElements()) {
            SubFields.push_back(std::make_pair(value, Aty->getNumElements()));
          }

          Ty = Aty->getArrayElementType();
        }
      }

      if (SubFields.size() > 0) {
        FieldChecks.push_back(FieldCheck(Gep, SubFields));
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
  if (auto *BC = dyn_cast<BitCastInst>(V))
    return getSourceImpl(BC->getOperand(0));

  if (auto *GEP = dyn_cast<GetElementPtrInst>(V))
    return getSourceImpl(GEP->getPointerOperand());

  if (auto *GEPO = dyn_cast<GEPOperator>(V))
    return getSourceImpl(GEPO->getPointerOperand());

  if (auto *BCO = dyn_cast<BitCastOperator>(V))
    return getSourceImpl(BCO->getOperand(0));

  if (auto *Phi = dyn_cast<PHINode>(V)) {
    Value *Source = nullptr;
    SmallPtrSet<Value *, 16> Visited;
    if (getPhiSource(Phi, Source, Visited))
      return Source;
    return Phi;
  }

  return V;
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

  ASSERT(AL != AddrLoc::kAddrLocUnknown);
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

void OverflowDefense::collectChunkCheck(
    Function &F, LoopInfo &LI, ObjectSizeOffsetEvaluator &ObjSizeEval) {
  DenseMap<Value *, SmallVector<Instruction *, 16>> SourceMap;

  for (auto &I : GepToInstrument) {
    if (isAccessMember(I))
      continue;
    Value *Source = getSource(I);
    SourceMap[Source].push_back(I);
  }

  for (auto &I : BcToInstrument) {
    Value *Source = getSource(I);
    SourceMap[Source].push_back(I);
  }

  for (auto &[Src, Insts] : SourceMap) {
    ASSERT(Src != nullptr);
    collectChunkCheckImpl(F, Src, Insts, LI, ObjSizeEval);
  }
}

bool OverflowDefense::isAccessMember(Instruction *I) {
  ASSERT(isa<GetElementPtrInst>(I));
  auto *GEP = cast<GetElementPtrInst>(I);
  if (!isFixedSizeType(GEP->getSourceElementType()))
    return false;

  if (auto C = dyn_cast<ConstantInt>(GEP->getOperand(1)))
    return C->getZExtValue() == 0;
  return false;
}

void OverflowDefense::collectChunkCheckImpl(
    Function &F, Value *Src, SmallVector<Instruction *, 16> &Insts,
    LoopInfo &LI, ObjectSizeOffsetEvaluator &ObjSizeEval) {
  if (tryRuntimeFreeCheck(F, Src, Insts, ObjSizeEval)) {
    return;
  }

  int weight = 0;
  for (auto *I : Insts)
    weight += LI.getLoopFor(I->getParent()) != nullptr ? 5 : 1;

  if (weight <= 2) {
    ChunkChecks.push_back(ChunkCheck(kRuntimeCheck, Src, Insts));
  } else {
    ChunkChecks.push_back(ChunkCheck(kClusterCheck, Src, Insts));
  }
}

void OverflowDefense::instrumentCluster(Function &F, Value *Src,
                                        SmallVector<Instruction *, 16> &Insts) {
  // Fetch Src Range from shadow memory.
  ASSERT(isa<Instruction>(Src) || isa<Argument>(Src));

  Instruction *InsertPt =
      isa<Instruction>(Src)
          ? cast<Instruction>(Src)->getInsertionPointAfterDef()
          : &*F.getEntryBlock().getFirstInsertionPt();

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  // Shadow = Ptr & kShadowMask;
  // Base = Ptr & kShadowBase;
  // Packed = *(int64_t *) Shadow;
  // Front = Packed & 0xffffffff;
  // Back = Packed >> 32;
  // Begin = Base - (Front << 3);
  // End = Base + (Back << 3);

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);

  // The final version of the code is commented out. The reason is that the
  // compiler is not able to support stack and global variables checking now
  /*
    Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
    Value *Shadow = IRB.CreateAnd(Ptr,
                                  ConstantInt::get(int64Type, kShadowMask));
    Value *Packed = IRB.CreateLoad(int64Type,
                                   IRB.CreateIntToPtr(Shadow, int64PtrType));
    Value *Front = IRB.CreateAnd(Packed,
                                 ConstantInt::get(int64Type, 0xffffffff));
    Value *Back = IRB.CreateLShr(Packed, 32);
    Value *Begin = IRB.CreateSub(Base, IRB.CreateShl(Front, 3));
    Value *End = IRB.CreateAdd(Base, IRB.CreateShl(Back, 3));
  */

  Value *IsApp = IRB.CreateAnd(
      IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kAllocatorSpaceBegin)),
      IRB.CreateICmpULT(Ptr,
                        ConstantInt::get(int64Type, kAllocatorStackBegin)));

  ASSERT(isa<Instruction>(IsApp));
  Instruction *ThenInsertPt = SplitBlockAndInsertIfThen(IsApp, InsertPt, false);
  IRB.SetInsertPoint(ThenInsertPt);
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Packed =
      IRB.CreateLoad(int64Type, IRB.CreateIntToPtr(Shadow, int64PtrType));
  Value *Front = IRB.CreateAnd(Packed, ConstantInt::get(int64Type, 0xffffffff));
  Value *Back = IRB.CreateLShr(Packed, 32);
  Value *ThenBegin = IRB.CreateSub(Base, IRB.CreateShl(Front, 3));
  Value *ThenEnd = IRB.CreateAdd(Base, IRB.CreateShl(Back, 3));

  IRB.SetInsertPoint(InsertPt);
  PHINode *Begin = IRB.CreatePHI(int64Type, 2);
  Begin->addIncoming(ThenBegin, ThenInsertPt->getParent());
  Begin->addIncoming(ConstantInt::get(int64Type, 0),
                     cast<Instruction>(IsApp)->getParent());

  PHINode *End = IRB.CreatePHI(int64Type, 2);
  End->addIncoming(ThenEnd, ThenInsertPt->getParent());
  End->addIncoming(ConstantInt::get(int64Type, kMaxAddress),
                   cast<Instruction>(IsApp)->getParent());

  // Check if Ptr is in [Begin, End).
  for (auto *I : Insts) {
    IRB.SetInsertPoint(I->getInsertionPointAfterDef());
    Value *Ptr = IRB.CreatePtrToInt(I, int64Type);
    Value *NotIn = IRB.CreateAnd(IRB.CreateICmpULT(Ptr, Begin),
                                 IRB.CreateICmpUGE(Ptr, End));
    CreateTrapBB(IRB, NotIn, true);
  }
}

bool OverflowDefense::tryRuntimeFreeCheck(
    Function &F, Value *Src, SmallVector<Instruction *, 16> &Insts,
    ObjectSizeOffsetEvaluator &ObjSizeEval) {
  SizeOffsetEvalType SizeOffsetEval = ObjSizeEval.compute(Src);

  if (ObjSizeEval.bothKnown(SizeOffsetEval)) {
    ChunkChecks.push_back(ChunkCheck(kBuiltInCheck, Src, Insts,
                                     SizeOffsetEval.first,
                                     SizeOffsetEval.second));
    return true;
  } else if (auto *G = dyn_cast<GlobalVariable>(Src)) {
    // FIXME: handle global variable
    return true;
  }

  return false;
}

void OverflowDefense::instrumentBitCast(Value *Src, BitCastInst *BC) {
  // ShadowAddr = BC & kShadowMask;
  // Base = BC & kShadowBase;
  // BackSize = *(int32_t *) ShadowAddr;
  // if (BC >= Base + BackSize - NeededSize)
  //   report_overflow();

  Instruction *InsertPt = BC->getInsertionPointAfterDef();
  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *CmpPtr = IRB.CreatePtrToInt(BC, int64Type);

  // TODO: this part will be removed
  Value *IsApp = IRB.CreateAnd(
      IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kAllocatorSpaceBegin)),
      IRB.CreateICmpULT(Ptr,
                        ConstantInt::get(int64Type, kAllocatorStackBegin)));
  IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));

  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *BackSize = IRB.CreateZExt(
      IRB.CreateLoad(int32Type, IRB.CreateIntToPtr(Shadow, int32PtrType)),
      int64Type);

  uint64_t NeededSize = DL->getTypeStoreSize(BC->getType());
  Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);

  Value *Cmp = IRB.CreateICmpUGE(
      CmpPtr, IRB.CreateSub(IRB.CreateAdd(Base, BackSize), NeededSizeVal));
  CreateTrapBB(IRB, Cmp, true);
}

void OverflowDefense::instrumentGep(Value *Src, GetElementPtrInst *GEP) {
  // ShadowAddr = GEP & kShadowMask;
  // Base = GEP & kShadowBase;
  // Packed = *(int32_t *) ShadowAddr;
  // Front = Packed & 0xffffffff;
  // Back = Packed >> 32;
  // Begin = Base - (Front << 3);
  // End = Base + (Back << 3);
  // if (GEP < Begin || GEP + NeededSize >= End)
  //   report_overflow();

  Instruction *InsertPt = GEP->getInsertionPointAfterDef();
  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *CmpPtr = IRB.CreatePtrToInt(GEP, int64Type);

  // TODO: this part will be removed
  Value *IsApp = IRB.CreateAnd(
      IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kAllocatorSpaceBegin)),
      IRB.CreateICmpULT(Ptr,
                        ConstantInt::get(int64Type, kAllocatorStackBegin)));
  IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));

  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *Packed =
      IRB.CreateLoad(int64Type, IRB.CreateIntToPtr(Shadow, int64PtrType));
  Value *Front = IRB.CreateAnd(Packed, ConstantInt::get(int64Type, 0xffffffff));
  Value *Back = IRB.CreateLShr(Packed, 32);
  Value *Begin = IRB.CreateSub(Base, IRB.CreateShl(Front, 3));
  Value *End = IRB.CreateAdd(Base, IRB.CreateShl(Back, 3));
  Value *CmpBegin = IRB.CreateICmpULT(CmpPtr, Begin);
  Value *CmpEnd = IRB.CreateICmpUGE(CmpPtr, End);
  Value *Cmp = IRB.CreateOr(CmpBegin, CmpEnd);
  CreateTrapBB(IRB, Cmp, true);
}

void OverflowDefense::replaceAlloca(Function &F) {
  if (AllocaToReplace.empty())
    return;

  dbgs() << "[" << F.getName()
         << "] AllocaToReplace: " << AllocaToReplace.size() << "\n";
}

void OverflowDefense::CreateTrapBB(BuilderTy &IRB, Value *Cond, bool Abort) {
  if (Abort && !Recover) {
    IRB.SetInsertPoint(
        SplitBlockAndInsertIfThen(Cond, &*IRB.GetInsertPoint(), true));

    IRB.CreateCall(AbortFn);
  } else {
    IRB.SetInsertPoint(
        SplitBlockAndInsertIfThen(Cond, &*IRB.GetInsertPoint(), false));

    IRB.CreateCall(ReportFn);
  }
}

void OverflowDefense::commitInstrument(Function &F) {
  for (auto &FC : FieldChecks)
    commitFieldCheck(F, FC);

  for (auto &CC : ChunkChecks)
    commitChunkCheck(F, CC);

  // Replace alloca with malloc
  replaceAlloca(F);
}

void OverflowDefense::commitFieldCheck(Function &F, FieldCheck &FC) {
  if (!ClCheckInField)
    return;

  Counter[kInFieldCheck]++;

  Instruction *InsertPt = FC.Gep;
  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Cond = nullptr;
  for (auto &SF : FC.SubFields) {
    Value *Cmp =
        IRB.CreateICmpUGE(SF.first, ConstantInt::get(int64Type, SF.second));
    Cond = Cond ? IRB.CreateOr(Cond, Cmp) : Cmp;
  }

  CreateTrapBB(IRB, Cond, true);
}

void OverflowDefense::commitChunkCheck(Function &F, ChunkCheck &CC) {
  if (CC.Type == kBuiltInCheck) {
    commitBuiltInCheck(F, CC);
  } else if (CC.Type == kClusterCheck) {
    commitClusterCheck(F, CC);
  } else if (CC.Type == kRuntimeCheck) {
    commitRuntimeCheck(F, CC);
  }
}

void OverflowDefense::commitBuiltInCheck(Function &F, ChunkCheck &CC) {
  if (!ClCheckStack)
    return;

  Counter[CC.Type]++;

  Value *Src = CC.Src;
  Instruction *InsertPt =
      isa<Instruction>(Src)
          ? cast<Instruction>(Src)->getInsertionPointAfterDef()
          : &*F.getEntryBlock().getFirstInsertionPt();

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Size = CC.Size;
  Value *Offset = CC.Offset;

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *PtrBegin = IRB.CreateSub(Ptr, Offset);
  Value *PtrEnd = IRB.CreateAdd(Ptr, Size);

  for (auto &I : CC.Insts) {
    IRB.SetInsertPoint(I->getInsertionPointAfterDef());

    uint64_t NeededSize = DL->getTypeStoreSize(I->getType());
    Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);

    Value *Addr = IRB.CreatePtrToInt(I, int64Type);
    Value *CmpBegin = IRB.CreateICmpULT(Addr, PtrBegin);
    Value *CmpEnd =
        IRB.CreateICmpUGE(Addr, IRB.CreateSub(PtrEnd, NeededSizeVal));
    Value *Cmp = IRB.CreateOr(CmpBegin, CmpEnd);

    CreateTrapBB(IRB, Cmp, true);
  }
}

void OverflowDefense::commitClusterCheck(Function &F, ChunkCheck &CC) {
  if (!ClCheckHeap)
    return;

  Counter[CC.Type]++;

  Value *Src = CC.Src;
  Instruction *InsertPt =
      isa<Instruction>(Src)
          ? cast<Instruction>(Src)->getInsertionPointAfterDef()
          : &*F.getEntryBlock().getFirstInsertionPt();

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  // Shadow = Ptr & kShadowMask;
  // Base = Ptr & kShadowBase;
  // Packed = *(int64_t *) Shadow;
  // Front = Packed & 0xffffffff;
  // Back = Packed >> 32;
  // Begin = Base - (Front << 3);
  // End = Base + (Back << 3);

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);

  // The final version of the code is commented out. The reason is that the
  // compiler is not able to support stack and global variables checking now
  /*
    Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type,
    kShadowBase)); Value *Shadow = IRB.CreateAnd(Ptr,
                                  ConstantInt::get(int64Type, kShadowMask));
    Value *Packed = IRB.CreateLoad(int64Type,
                                   IRB.CreateIntToPtr(Shadow, int64PtrType));
    Value *Front = IRB.CreateAnd(Packed,
                                 ConstantInt::get(int64Type, 0xffffffff));
    Value *Back = IRB.CreateLShr(Packed, 32);
    Value *Begin = IRB.CreateSub(Base, IRB.CreateShl(Front, 3));
    Value *End = IRB.CreateAdd(Base, IRB.CreateShl(Back, 3));
  */

  Value *IsApp = IRB.CreateAnd(
      IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kAllocatorSpaceBegin)),
      IRB.CreateICmpULT(Ptr,
                        ConstantInt::get(int64Type, kAllocatorStackBegin)));

  ASSERT(isa<Instruction>(IsApp));
  Instruction *ThenInsertPt = SplitBlockAndInsertIfThen(IsApp, InsertPt, false);
  IRB.SetInsertPoint(ThenInsertPt);
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Packed =
      IRB.CreateLoad(int64Type, IRB.CreateIntToPtr(Shadow, int64PtrType));
  Value *Front = IRB.CreateAnd(Packed, ConstantInt::get(int64Type, 0xffffffff));
  Value *Back = IRB.CreateLShr(Packed, 32);
  Value *ThenBegin = IRB.CreateSub(Base, IRB.CreateShl(Front, 3));
  Value *ThenEnd = IRB.CreateAdd(Base, IRB.CreateShl(Back, 3));

  IRB.SetInsertPoint(InsertPt);
  PHINode *Begin = IRB.CreatePHI(int64Type, 2);
  Begin->addIncoming(ThenBegin, ThenInsertPt->getParent());
  Begin->addIncoming(ConstantInt::get(int64Type, 0),
                     cast<Instruction>(IsApp)->getParent());

  PHINode *End = IRB.CreatePHI(int64Type, 2);
  End->addIncoming(ThenEnd, ThenInsertPt->getParent());
  End->addIncoming(ConstantInt::get(int64Type, kMaxAddress),
                   cast<Instruction>(IsApp)->getParent());

  // Check if Ptr is in [Begin, End).
  for (auto *I : CC.Insts) {
    IRB.SetInsertPoint(I->getInsertionPointAfterDef());
    Value *Ptr = IRB.CreatePtrToInt(I, int64Type);
    Value *NotIn = IRB.CreateAnd(IRB.CreateICmpULT(Ptr, Begin),
                                 IRB.CreateICmpUGE(Ptr, End));
    CreateTrapBB(IRB, NotIn, true);
  }
}

void OverflowDefense::commitRuntimeCheck(Function &F, ChunkCheck &CC) {
  if (!ClCheckHeap)
    return;

  Counter[CC.Type] += CC.Insts.size();

  Value *Src = CC.Src;

  for (auto *I : CC.Insts) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      instrumentGep(Src, GEP);
    } else if (auto *BC = dyn_cast<BitCastInst>(I)) {
      instrumentBitCast(Src, BC);
    }
  }
}