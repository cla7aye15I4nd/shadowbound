//===- ShadowBound.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/ShadowBound.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Identification.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <string>

using namespace llvm;
using BuilderTy = IRBuilder<TargetFolder>;

#define DEBUG_TYPE "shadowbound"

STATISTIC(NumRuntimeChecks, "Number of shadow-memory bounds checks emitted");
STATISTIC(NumClusterChecks, "Number of clustered shadow-memory bound loads");
STATISTIC(NumBuiltinChecks, "Number of checks against a statically known size");
STATISTIC(NumNonHeapElided,
          "Number of checks dropped because the source is never a heap pointer");
STATISTIC(NumIntermediateElided,
          "Number of checks dropped on pointers only used to derive checked "
          "pointers");
STATISTIC(NumStructHeuristicElided,
          "Number of checks dropped by the struct-field heuristic");
STATISTIC(NumIPONonHeapArgs, "Number of arguments proven never heap pointers");
STATISTIC(NumIPONonHeapRets,
          "Number of functions proven to never return a heap pointer");

// Please use this macro instead of assert()
#define ASSERT(X)                                                              \
  do {                                                                         \
    if (!(X)) {                                                                \
      printf("Assertion failed: " #X "\n");                                    \
      abort();                                                                 \
    }                                                                          \
  } while (0)

// The runtime allocator keeps kReservedBytes after and kFrontReservedBytes
// before every heap object, inside its shadow bounds.
static const int kReservedBytes = 0x20;
static const int kFrontReservedBytes = 0x20;

static const uint64_t kShadowBase = ~0x7ULL;
static const uint64_t kShadowMask = ~0x400000000007ULL;
static const uint64_t kHeapSpaceBeg = 0x600000000000ULL;
static const uint64_t kHeapSpaceEnd = 0x700000000000ULL;
static const uint64_t kMaxAddress = 0x1000000000000ULL;

// ===== Modification in Different Mode =====
// +-----------------+--------------+-----------------------+
// | Name            | Instrument   | Runtime Check         |
// +-----------------+--------------+-----------------------+
// | Normal          | N/A          | N/A                   |
// +-----------------+--------------+-----------------------+
// | Keep Going      | CreateTrapBB | check_range           |
// +-----------------+--------------+-----------------------+
// | Skip Instrument | No Instrument| check_range/SetShadow |
// +-----------------+--------------+-----------------------+
// | Perf Test       | N/A          | SetShadow             |
// +-----------------+--------------+-----------------------+

static cl::opt<bool> ClKeepGoing("shadowbound-keep-going",
                                 cl::desc("keep going after reporting a error"),
                                 cl::Hidden, cl::init(false));

static cl::opt<bool> ClSkipInstrument("shadowbound-skip-instrument",
                                      cl::desc("skip instrumenting"),
                                      cl::Hidden, cl::init(false));

static cl::opt<bool> ClPerfTest("shadowbound-perf-test", cl::desc("performance test"),
                                cl::Hidden, cl::init(false));
// Please note that due to limitations in the current implementation, we cannot
// guarantee that all corresponding checks will be disabled when the
// shadowbound-check-[heap|stack|global] option is set to false. However, in most
// cases, the majority of these checks will be skipped.

// ==== Check Type Option ==== //
static cl::opt<bool> ClCheckHeap("shadowbound-check-heap",
                                 cl::desc("check heap memory"), cl::Hidden,
                                 cl::init(true));

// ==== Optimization Option ==== //
static cl::opt<bool> ClOnlySmallAllocOpt("shadowbound-only-small-alloc-opt",
                                         cl::desc("optimize only small alloc"),
                                         cl::Hidden, cl::init(true));

static cl::opt<bool> ClLoopOpt("shadowbound-loop-opt",
                               cl::desc("optimize loop checks"), cl::Hidden,
                               cl::init(false));

static cl::opt<bool> ClReserveOpt("shadowbound-reserve-opt",
                                  cl::desc("optimize reserved pointer checks"),
                                  cl::Hidden, cl::init(true));

static cl::opt<bool> ClDirectionOpt("shadowbound-direction-opt",
                                    cl::desc("optimize direction checks"),
                                    cl::Hidden, cl::init(true));

static cl::opt<bool> ClPatternOpt("shadowbound-pattern-opt",
                                  cl::desc("optimize pattern checks"),
                                  cl::Hidden, cl::init(true));

static cl::opt<std::string> ClPatternOptFile("shadowbound-pattern-opt-file",
                                             cl::desc("pattern opt file"),
                                             cl::Hidden, cl::init(""));

static cl::opt<bool> ClMergeOpt("shadowbound-merge-opt",
                                cl::desc("optimize merge checks"), cl::Hidden,
                                cl::init(true));

static cl::opt<bool> ClDependenceOpt("shadowbound-dependence-opt",
                                     cl::desc("optimize dependence checks"),
                                     cl::Hidden, cl::init(true));

static cl::opt<bool> ClTailCheck("shadowbound-tail-check",
                                 cl::desc("check tail of array"), cl::Hidden,
                                 cl::init(false));

// ==== Debug Option ==== //
static cl::opt<std::string> ClWhiteList("shadowbound-whitelist",
                                        cl::desc("whitelist file"), cl::Hidden,
                                        cl::init(""));

static cl::opt<bool> ClNonHeapOpt(
    "shadowbound-nonheap-opt",
    cl::desc("drop checks whose source is provably never a heap pointer, "
             "using the interprocedural shadowbound-ipo analysis"),
    cl::Hidden, cl::init(true));

static cl::opt<bool> ClIntermediateOpt(
    "shadowbound-intermediate-opt",
    cl::desc("do not check pointers that are only used to derive other "
             "checked pointers"),
    cl::Hidden, cl::init(true));

static cl::opt<bool> ClStructHeuristic(
    "shadowbound-struct-heuristic",
    cl::desc("UNSOUND: trust pointers loaded from a struct field that an "
             "allocation sized by a loaded length is stored into (the "
             "\"struct\" pattern of the old out-of-tree analyzer)"),
    cl::Hidden, cl::init(false));

static cl::opt<bool> ClDumpIR("shadowbound-dump-ir", cl::desc("dump IR"), cl::Hidden,
                              cl::init(false));

const char kShadowBoundModuleCtorName[] = "shadowbound.module_ctor";
const char kShadowBoundInitName[] = "__shadowbound_init";
const char kShadowBoundReportName[] = "__shadowbound_report";
const char kShadowBoundAbortName[] = "__shadowbound_abort";
const char kShadowBoundSetShadowName[] = "__shadowbound_set_shadow";

// Module flag value bits written by the LTO pre-link step.
static constexpr uint32_t kLTOFlagEnabled = 1;
static constexpr uint32_t kLTOFlagRecover = 2;

const char llvm::kShadowBoundLTOMarker[] = "shadowbound-lto";

namespace {

enum CheckType {
  kRuntimeCheck = 0,
  kClusterCheck = 1,
  kBuiltInCheck = 2,
  kCheckTypeEnd
};

using OffsetDir = uint8_t;
static constexpr OffsetDir kOffsetUnknown = 0b00;
static constexpr OffsetDir kOffsetPositive = 0b01;
static constexpr OffsetDir kOffsetNegative = 0b10;
static constexpr OffsetDir kOffsetBoth = kOffsetPositive | kOffsetNegative;

struct BaseCheck {
  enum CheckType Type;

  BaseCheck() = delete;
  BaseCheck(enum CheckType Type) : Type(Type) {}
};

struct ClusterCheck : public BaseCheck {
  Value *Src;
  Instruction *InsertPt;
  SmallVector<Instruction *, 16> Insts;

  ClusterCheck(Value *Src, Instruction *InsertPt,
               SmallVector<Instruction *, 16> Insts)
      : BaseCheck(kClusterCheck), Src(Src), InsertPt(InsertPt), Insts(Insts) {}
};

struct RuntimeCheck : public BaseCheck {
  Value *Src;
  SmallVector<Instruction *, 16> Insts;

  RuntimeCheck(Value *Src, SmallVector<Instruction *, 16> Insts)
      : BaseCheck(kRuntimeCheck), Src(Src), Insts(Insts) {}
};

struct BuiltinCheck : public BaseCheck {
  Value *Src;
  Value *Size;
  Value *Offset;
  SmallVector<Instruction *, 16> Insts;

  BuiltinCheck(Value *Src, Value *Size, Value *Offset,
               SmallVector<Instruction *, 16> Insts)
      : BaseCheck(kBuiltInCheck), Src(Src), Size(Size), Offset(Offset),
        Insts(Insts) {}
};

struct MonoLoop {
  Loop *Lop;
  PHINode *IndVar;
  Value *Lower;
  Value *Upper;
  Value *Step;

  BasicBlock *GuardBB;
  BasicBlock *Preheader;

  MonoLoop(Loop *Lop, PHINode *IndVar, Value *Lower, Value *Upper, Value *Step,
           BasicBlock *GuardBB, BasicBlock *Preheader)
      : Lop(Lop), IndVar(IndVar), Lower(Lower), Upper(Upper), Step(Step),
        GuardBB(GuardBB), Preheader(Preheader) {}

  Value *getStepInst() const {
    return IndVar->getIncomingValueForBlock(Lop->getLoopLatch());
  }
};

enum PtrUsage {
  kPtrNone,
  kPtrDeref,
  kPtrEscape,
};
class ShadowBound {
public:
  ShadowBound(Module &M, const ShadowBoundOptions &Options,
              const ShadowBoundIPOInfo *IPO)
      : IPO(IPO), Options(Options) {
    initializeModule(M);
  }

  ShadowBound(const ShadowBound &&) = delete;
  ShadowBound &operator=(const ShadowBound &&) = delete;
  ShadowBound(const ShadowBound &) = delete;
  ShadowBound &operator=(const ShadowBound &) = delete;

  bool sanitizeFunction(Function &F, FunctionAnalysisManager &FAM);

private:
  void initializeModule(Module &M);
  void collectToInstrument(Function &F, ObjectSizeOffsetEvaluator &ObjSizeEval,
                           ScalarEvolution &SE);

  bool filterToInstrument(Function &F, Instruction *I,
                          ObjectSizeOffsetEvaluator &ObjSizeEval,
                          ScalarEvolution &SE);
  PtrUsage GetPtrUsage(Instruction *I);
  bool isZeroAccessGep(const DataLayout *DL, Instruction *I);
  bool isShrinkBitCast(Instruction *I);
  bool isSafePointer(Instruction *Ptr, ObjectSizeOffsetEvaluator &ObjSizeEval,
                     ScalarEvolution &SE);
  bool isSafeFieldAccess(Instruction *I);
  bool isAccessMember(Instruction *I);
  bool isAccessMemberBoost(Instruction *I, ScalarEvolution &SE);
  void structPointerOptimizae(Function &F, ScalarEvolution &SE);
  void ipoOptimize(Function &F);
  bool patternMatch(Function &F, Instruction *I, PatternBase *P);
  void patternOptimize(Function &F);
  void dependencyOptimize(Function &F, DominatorTree &DT,
                          PostDominatorTree &PDT, ScalarEvolution &SE);
  void loopOptimize(Function &F, LoopInfo &LI, ScalarEvolution &SE,
                    DominatorTree &DT, PostDominatorTree &PDT);
  void collectMonoLoop(Function &F, LoopInfo &LI, ScalarEvolution &SE);
  bool monotonicLoopOptimize(Function &F, Value *Addr, Loop *L,
                             ScalarEvolution &SE, DominatorTree &DT);

  SmallVector<BitCastInst *, 16> dependencyOptimizeForBc(Function &F,
                                                         DominatorTree &DT,
                                                         PostDominatorTree &PDT,
                                                         ScalarEvolution &SE);

  SmallVector<GetElementPtrInst *, 16>
  dependencyOptimizeForGep(Function &F, DominatorTree &DT,
                           PostDominatorTree &PDT, ScalarEvolution &SE);
  void collectChunkCheck(Function &F, LoopInfo &LI,
                         ObjectSizeOffsetEvaluator &ObjSizeEval,
                         ScalarEvolution &SE, DominatorTree &DT);
  void collectChunkCheckImpl(Function &F, Value *Src,
                             SmallVector<Instruction *, 16> &Insts,
                             LoopInfo &LI,
                             ObjectSizeOffsetEvaluator &ObjSizeEval,
                             ScalarEvolution &SE, DominatorTree &DT);
  bool tryRuntimeFreeCheck(Function &F, Value *Src,
                           SmallVector<Instruction *, 16> &Insts,
                           ObjectSizeOffsetEvaluator &ObjSizeEval);

  void dropIntermediateChecks();
  Instruction *getCheckInsertPt(Instruction *I);
  SelectInst *getOnlySelectUser(Instruction *I);
  Value *guardBySelect(SelectInst *S, Instruction *I, Value *Cmp,
                       BuilderTy &IRB);
  void commitInstrument(Function &F);
  void commitBuiltInCheck(Function &F, BuiltinCheck &Check);
  void commitClusterCheck(Function &F, ClusterCheck &Check);
  void commitRuntimeCheck(Function &F, RuntimeCheck &Check);

  void instrumentBitCast(Function &F, Value *Src, BitCastInst *BC);
  void instrumentGep(Function &F, Value *Src, GetElementPtrInst *GEP);

  Value *makeOverflowCmp(BuilderTy &IRB, Value *CmpPtr, Value *End,
                         uint64_t NeededSize, bool OnlyStored = false);
  static bool isOnlyStored(Instruction *I);
  void getPointerBeginEnd(Value *Ptr, Value *&Begin, Value *&End,
                          BuilderTy &IRB);
  void getPointerBegin(Value *Ptr, Value *&Begin, BuilderTy &IRB);
  void getPointerEnd(Value *Ptr, Value *&End, BuilderTy &IRB);
  Value *getPointerIsApp(Value *Ptr, BuilderTy &IRB);

  Value *getSource(Value *I);
  Value *getSourceImpl(Value *V);
  OffsetDir getOffsetDir(Value *Addr);
  void setOffsetDir(Value *Addr, ScalarEvolution &SE);
  bool getPhiSource(Value *V, Value *&Src, SmallPtrSet<Value *, 16> &Visited);

  void CreateTrapBB(BuilderTy &B, Value *Cond, bool Abort);
  Value *readRegister(Function &F, BuilderTy &IRB, StringRef RegName);

  StructType *sourceAnalysis(Function &F, Value *Src);

  SmallVector<GetElementPtrInst *, 16> GepToInstrument;
  SmallVector<BitCastInst *, 16> BcToInstrument;

  DenseMap<Value *, OffsetDir> OffsetDirCache;
  // Directions whose check can be dropped because a dominating check on the
  // same base already covers them (redundant-check elimination).
  DenseMap<Value *, OffsetDir> DroppedDir;
  // Checks that another check was dropped (fully or one direction) in favour
  // of; they must stay even if they look redundant otherwise.
  SmallPtrSet<Instruction *, 16> ReliedUpon;
  DenseMap<Value *, Value *> SourceCache;
  DenseMap<Value *, PtrUsage> PtrUsageCache;
  DenseMap<Loop *, MonoLoop *> MonoLoopMap;
  SmallVector<BaseCheck *, 16> Checks;

  StringSet<> WhiteList;

  const DataLayout *DL;

  int Counter[kCheckTypeEnd];

  Type *int32Type;
  Type *int64Type;
  Type *int32PtrType;
  Type *int64PtrType;

  Function *ReportFn;
  Function *AbortFn;
  Function *SetShadowFn;

  // Interprocedural facts, when the shadowbound-ipo analysis was run on the
  // module beforehand (it is not available from a lone function pass).
  const ShadowBoundIPOInfo *IPO;
  LoopInfo *LoopI = nullptr;

  ShadowBoundOptions Options;
};

bool isEscapeInstruction(Instruction *I, Value *V) {
  if (auto *RI = dyn_cast<ReturnInst>(I)) {
    ASSERT(RI->getReturnValue() == V);
    return true;
  }

  if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (SI->getValueOperand() == V)
      return true;
  }

  static SmallVector<StringRef, 16> whitelist = {
      // LLVM Intrinsics
      "llvm.prefetch.",
      // allocate/free
      "realloc",
      "free",
  };

  if (auto *CI = dyn_cast<CallInst>(I)) {
    if (CI->isLifetimeStartOrEnd())
      return false;

    Function *F = CI->getCalledFunction();
    if (F != nullptr) {
      for (auto &name : whitelist) {
        if (F->getName().startswith(name))
          return false;
      }
    }

    return true;
  }

  return false;
}

bool isDerefInstruction(Instruction *I, Value *V) {
  if (auto *LI = dyn_cast<LoadInst>(I)) {
    ASSERT(LI->getPointerOperand() == V);
    return true;
  }

  if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (SI->getPointerOperand() == V)
      return true;
  }

  // Atomics dereference their pointer operand just like load/store.
  if (auto *AI = dyn_cast<AtomicRMWInst>(I))
    return AI->getPointerOperand() == V;
  if (auto *CX = dyn_cast<AtomicCmpXchgInst>(I))
    return CX->getPointerOperand() == V;

  return false;
}

bool isUnionType(Type *Ty) {
  if (auto *STy = dyn_cast<StructType>(Ty))
    return STy->hasName() && STy->getName().startswith("union.");
  else
    return false;
}

bool isFlexibleStructure(StructType *STy) {
  if (STy->getNumElements() == 0)
    return false;

  if (ArrayType *Aty = dyn_cast<ArrayType>(STy->elements().back())) {
    // Avoid Check Some Flexible Array Member
    // struct page_entry {
    //    ...
    //    unsigned long in_use_p[1];
    // } page_entry;

    // It the number of elements is less or equal to 1, it usually means it is
    // a flexible structure.
    return Aty->getNumElements() <= 1;
  }

  return false;
}

bool isVirtualTableGep(Instruction *I) {
  // A C++ virtual dispatch loads the vtable pointer from the object and then
  // indexes it with a COMPILE-TIME-CONSTANT slot. A variable index into an
  // array of function pointers (e.g. a C callback table `tbl[i](...)`) is an
  // ordinary array access and must still be bounds-checked, so only skip the
  // constant-index case here.
  if (auto *Gep = dyn_cast<GetElementPtrInst>(I)) {
    if (!Gep->hasAllConstantIndices())
      return false;
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

unsigned getFixedSize(Type *Ty, const DataLayout *DL) {
  if (Ty->isArrayTy() || Ty->isStructTy())
    return DL->getTypeStoreSize(Ty);

  ASSERT(false);
}

bool isFixedSizeType(Type *Ty) {
  if (isUnionType(Ty))
    return false;

  if (Ty->isArrayTy())
    return true;

  if (StructType *STy = dyn_cast<StructType>(Ty))
    return !isFlexibleStructure(STy);

  return false;
}

// An insertion point in the entry block after all static allocas. Splitting the
// entry block above a static alloca would move it into a non-entry block and
// turn it into a dynamic alloca, so checks on an argument source are inserted
// here instead of at the very top of the entry block.
Instruction *entryInsertPtAfterAllocas(Function &F) {
  BasicBlock &Entry = F.getEntryBlock();
  Instruction *IP = &*Entry.getFirstInsertionPt();
  for (Instruction &I : Entry)
    if (auto *AI = dyn_cast<AllocaInst>(&I))
      if (AI->isStaticAlloca() && AI->getNextNode())
        IP = AI->getNextNode();
  return IP;
}

void insertModuleCtor(Module &M) {
  getOrCreateSanitizerCtorAndInitFunctions(
      M, kShadowBoundModuleCtorName, kShadowBoundInitName,
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
  M.getOrInsertFunction(kShadowBoundReportName, Type::getVoidTy(C));
  M.getOrInsertFunction(kShadowBoundAbortName, Type::getVoidTy(C));
  M.getOrInsertFunction(kShadowBoundSetShadowName, Type::getVoidTy(C),
                        Type::getInt64Ty(C), Type::getInt64Ty(C),
                        Type::getInt64Ty(C));
}

void insertGlobalVariable(Module &M) {
  LLVMContext &C = M.getContext();
  M.getOrInsertGlobal("__shadowbound_only_small_alloc_opt", Type::getInt32Ty(C), [&] {
    return new GlobalVariable(
        M, Type::getInt32Ty(C), true, GlobalValue::WeakODRLinkage,
        ConstantInt::get(Type::getInt32Ty(C), ClOnlySmallAllocOpt ? 1 : 0),
        "__shadowbound_only_small_alloc_opt");
  });
  M.getOrInsertGlobal("__shadowbound_keep_going", Type::getInt32Ty(C), [&] {
    return new GlobalVariable(
        M, Type::getInt32Ty(C), true, GlobalValue::WeakODRLinkage,
        ConstantInt::get(Type::getInt32Ty(C), ClKeepGoing ? 1 : 0),
        "__shadowbound_keep_going");
  });
  M.getOrInsertGlobal("__shadowbound_skip_instrument", Type::getInt32Ty(C), [&] {
    return new GlobalVariable(
        M, Type::getInt32Ty(C), true, GlobalValue::WeakODRLinkage,
        ConstantInt::get(Type::getInt32Ty(C), ClSkipInstrument ? 1 : 0),
        "__shadowbound_skip_instrument");
  });
  M.getOrInsertGlobal("__shadowbound_perf_test", Type::getInt32Ty(C), [&] {
    return new GlobalVariable(
        M, Type::getInt32Ty(C), true, GlobalValue::WeakODRLinkage,
        ConstantInt::get(Type::getInt32Ty(C), ClPerfTest ? 1 : 0),
        "__shadowbound_perf_test");
  });
}

template <class T> T getOptOrDefault(const cl::opt<T> &Opt, T Default) {
  return (Opt.getNumOccurrences() > 0) ? Opt : Default;
}
} // end anonymous namespace

ShadowBoundOptions::ShadowBoundOptions(bool Recover, bool LTOPostLink)
    : Recover(getOptOrDefault(ClKeepGoing, Recover)), LTOPostLink(LTOPostLink) {}

// The pass reads pointee types (getPointerElementType) everywhere, so it only
// works on typed pointers.
static bool hasTypedPointers(const Module &M) {
  return M.getContext().supportsTypedPointers();
}

PreservedAnalyses ShadowBoundPass::run(Function &F,
                                           FunctionAnalysisManager &FAM) {
  if (!hasTypedPointers(*F.getParent()))
    return PreservedAnalyses::all();

  // At link time only code that was compiled with ShadowBound is instrumented.
  if (Options.LTOPostLink && !F.hasFnAttribute(kShadowBoundLTOMarker))
    return PreservedAnalyses::all();

  const ShadowBoundIPOInfo *IPO =
      FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F)
          .getCachedResult<ShadowBoundIPOAnalysis>(*F.getParent());

  ShadowBound ShadowBound(*F.getParent(), Options, IPO);
  if (ShadowBound.sanitizeFunction(F, FAM))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

PreservedAnalyses ModuleShadowBoundPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  if (ClDumpIR) {
    std::error_code EC;
    raw_fd_ostream OS(M.getSourceFileName() + ".bc", EC, sys::fs::OF_None);
    WriteBitcodeToFile(M, OS);
  }

  if (!hasTypedPointers(M)) {
    M.getContext().emitError(
        "ShadowBound requires typed pointers; compile with "
        "-Xclang -no-opaque-pointers");
    return PreservedAnalyses::all();
  }

  insertModuleCtor(M);
  insertRuntimeFunction(M);
  insertGlobalVariable(M);
  return PreservedAnalyses::none();
}

void ShadowBound::initializeModule(Module &M) {
  LLVMContext &C = M.getContext();

  DL = &M.getDataLayout();

  M.getOrInsertFunction(kShadowBoundReportName, Type::getVoidTy(C));
  M.getOrInsertFunction(kShadowBoundAbortName, Type::getVoidTy(C));

  ReportFn = M.getFunction(kShadowBoundReportName);
  AbortFn = M.getFunction(kShadowBoundAbortName);
  SetShadowFn = M.getFunction(kShadowBoundSetShadowName);

  ASSERT(ReportFn != nullptr);
  ASSERT(AbortFn != nullptr);

  int32Type = Type::getInt32Ty(C);
  int64Type = Type::getInt64Ty(C);
  int32PtrType = Type::getInt32PtrTy(C);
  int64PtrType = Type::getInt64PtrTy(C);

  memset(Counter, 0, sizeof(Counter));

  // Initialize the white list
  std::string WhiteListPath = ClWhiteList;
  if (WhiteListPath != "") {
    std::ifstream WhiteListFile(WhiteListPath);
    if (WhiteListFile.is_open()) {
      std::string Line;
      while (std::getline(WhiteListFile, Line)) {
        Line.erase(std::remove_if(Line.begin(), Line.end(), isspace),
                   Line.end());
        WhiteList.insert(Line);
      }
      WhiteListFile.close();
    }
  }
}

bool ShadowBound::sanitizeFunction(Function &F,
                                       FunctionAnalysisManager &AM) {
  if (F.isIntrinsic())
    return false;

  if (F.getInstructionCount() == 0)
    return false;

  if (F.getName() == kShadowBoundInitName || F.getName() == kShadowBoundModuleCtorName)
    return false;

  // no_sanitize("shadowbound") / disable_sanitizer_instrumentation.
  if (F.hasFnAttribute("no_shadowbound") ||
      F.hasFnAttribute(Attribute::DisableSanitizerInstrumentation))
    return false;

  if (ClSkipInstrument)
    return false;

  if (isStdFunction(F.getName()))
    return false;

  if (WhiteList.find(F.getName()) != WhiteList.end())
    return false;

  LLVM_DEBUG(dbgs() << "[" << F.getName() << "]\n");

  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  LoopI = &LI;
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);

  ObjectSizeOpts EvalOpts;
  EvalOpts.RoundToAlign = true;
  ObjectSizeOffsetEvaluator ObjSizeEval(*DL, &TLI, F.getContext(), EvalOpts);

  // Collect all instructions to instrument
  collectToInstrument(F, ObjSizeEval, SE);
  ipoOptimize(F);

  dependencyOptimize(F, DT, PDT, SE);
  loopOptimize(F, LI, SE, DT, PDT);
  if (ClLoopOpt) {
    // Loop Optimization may introduce new instructions to instrument
    dependencyOptimize(F, DT, PDT, SE);
  }
  structPointerOptimizae(F, SE);
  patternOptimize(F);

  // Instrument GEP and BC
  collectChunkCheck(F, LI, ObjSizeEval, SE, DT);

  dropIntermediateChecks();
  commitInstrument(F);

  if (std::accumulate(Counter, Counter + kCheckTypeEnd, 0) > 0) {
    LLVM_DEBUG(dbgs() << "  Builtin Check: " << Counter[kBuiltInCheck] << "\n");
    LLVM_DEBUG(dbgs() << "  Cluster Check: " << Counter[kClusterCheck] << "\n");
    LLVM_DEBUG(dbgs() << "  Runtime Check: " << Counter[kRuntimeCheck] << "\n");
  }

  return true;
}

void ShadowBound::collectToInstrument(
    Function &F, ObjectSizeOffsetEvaluator &ObjSizeEval, ScalarEvolution &SE) {
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *Gep = dyn_cast<GetElementPtrInst>(&I)) {
        if (!filterToInstrument(F, Gep, ObjSizeEval, SE))
          GepToInstrument.push_back(Gep);
      } else if (auto *Bc = dyn_cast<BitCastInst>(&I)) {
        if (!filterToInstrument(F, Bc, ObjSizeEval, SE))
          BcToInstrument.push_back(Bc);
      }
    }
  }
}

bool ShadowBound::filterToInstrument(Function &F, Instruction *I,
                                         ObjectSizeOffsetEvaluator &ObjSizeEval,
                                         ScalarEvolution &SE) {
  if (!I->getType()->isPointerTy())
    return true;

  if (GetPtrUsage(I) == kPtrNone)
    return true;

  if (isShrinkBitCast(I))
    return true;

  if (isZeroAccessGep(DL, I))
    return true;

  if (isVirtualTableGep(I))
    return true;

  if (isSafePointer(I, ObjSizeEval, SE))
    return true;

  return false;
}

bool ShadowBound::isSafePointer(Instruction *Ptr,
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
  uint32_t NeededSize =
      DL->getTypeStoreSize(Ptr->getType()->getPointerElementType());
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

bool ShadowBound::isZeroAccessGep(const DataLayout *DL, Instruction *I) {
  if (!ClReserveOpt)
    return false;

  auto *Gep = dyn_cast<GetElementPtrInst>(I);

  // It is not a GEP
  if (Gep == nullptr)
    return false;

  APInt Offset(DL->getIndexSizeInBits(Gep->getPointerAddressSpace()), 0, true);
  if (!Gep->accumulateConstantOffset(*DL, Offset))
    return false;

  if (Offset.isNegative())
    return false;

  // The reserved bytes only cover this access if the WHOLE access (offset plus
  // the accessed size) fits within them. The previous code ignored the access
  // width, so e.g. an 8-byte load at offset 0x20 (ending at 0x28) was wrongly
  // elided. A pointer that escapes gets no tolerance at all.
  uint64_t Tolerance = GetPtrUsage(I) == kPtrDeref ? kReservedBytes : 0;
  uint64_t NeededSize =
      DL->getTypeStoreSize(Gep->getType()->getPointerElementType());
  return Offset.getZExtValue() + NeededSize <= Tolerance;
}

PtrUsage ShadowBound::GetPtrUsage(Instruction *I) {
  SmallVector<Instruction *, 16> WorkList;
  SmallPtrSet<Instruction *, 16> Visited;

  if (PtrUsageCache.count(I))
    return PtrUsageCache[I];

  // Follow every user that forwards the pointer (cast/GEP/phi/select) so we
  // find the eventual real use. A pointer that is only dereferenced can use
  // the reserved-bytes relaxation (kPtrDeref); a pointer that escapes, or that
  // has any user we do not understand, must keep the full check (kPtrEscape).
  // Only a value with no uses at all is kPtrNone.
  bool SawUse = false;
  PtrUsage Result = kPtrDeref;

  WorkList.push_back(I);
  while (!WorkList.empty()) {
    Instruction *V = WorkList.pop_back_val();

    if (Visited.count(V))
      continue;

    Visited.insert(V);

    for (auto *U : V->users()) {
      auto *UI = dyn_cast<Instruction>(U);
      if (UI == nullptr) {
        // Used by a constant expression / global initializer: be conservative.
        SawUse = true;
        Result = kPtrEscape;
        continue;
      }

      // A prefetch is only a cache hint: it never faults and reads nothing
      // the program sees, so its address need not be in bounds. x264 (SPEC
      // 525.x264_r) prefetches &mv[l][top_4x4 - 1] for the top macroblock
      // row, about 1.2 KB before the array.
      if (auto *II = dyn_cast<IntrinsicInst>(UI))
        if (II->getIntrinsicID() == Intrinsic::prefetch)
          continue;

      // Casts, GEPs, phis and selects only forward the pointer; whether it is
      // used is decided by their users.
      if (isa<BitCastInst>(UI) || isa<GetElementPtrInst>(UI) ||
          isa<PHINode>(UI) || isa<SelectInst>(UI)) {
        WorkList.push_back(UI);
        continue;
      }

      SawUse = true;

      if (isDerefInstruction(UI, V))
        continue;

      if (isEscapeInstruction(UI, V)) {
        Result = kPtrEscape;
        continue;
      }

      // A whitelisted call (free/realloc/prefetch/lifetime) does not create a
      // derived base pointer; isEscapeInstruction returned false for it. Any
      // other user (invoke, atomics on a different operand, ptrtoint, ...) is
      // treated conservatively as an escape so the pointer is still checked.
      if (auto *CI = dyn_cast<CallInst>(UI)) {
        (void)CI; // whitelisted call: ignore.
        continue;
      }

      Result = kPtrEscape;
    }
  }

  if (!SawUse)
    return PtrUsageCache[I] = kPtrNone;
  return PtrUsageCache[I] = Result;
}

bool ShadowBound::isShrinkBitCast(Instruction *I) {
  if (auto *BC = dyn_cast<BitCastInst>(I)) {
    if (!BC->getSrcTy()->isPointerTy() || !BC->getDestTy()->isPointerTy())
      return false;

    Type *srcTy = BC->getSrcTy()->getPointerElementType();
    Type *dstTy = BC->getDestTy()->getPointerElementType();

    if (isUnionType(srcTy) || isUnionType(dstTy))
      return true;

    if (!srcTy->isSized() || !dstTy->isSized())
      return true;

    if (auto *STy = dyn_cast<StructType>(srcTy))
      if (isFlexibleStructure(STy))
        return true;

    if (auto *STy = dyn_cast<StructType>(dstTy))
      if (isFlexibleStructure(STy))
        return true;

    TypeSize srcSize = DL->getTypeStoreSize(srcTy);
    TypeSize dstSize = DL->getTypeStoreSize(dstTy);

    // We always ensure every pointer holds at least `kReservedBytes` bytes.
    return dstSize <= srcSize || dstSize <= kReservedBytes;
  }

  return false;
}

void ShadowBound::dependencyOptimize(Function &F, DominatorTree &DT,
                                         PostDominatorTree &PDT,
                                         ScalarEvolution &SE) {

  if (!ClDependenceOpt)
    return;
  SmallVector<BitCastInst *, 16> NewBcToInstrument =
      dependencyOptimizeForBc(F, DT, PDT, SE);
  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument =
      dependencyOptimizeForGep(F, DT, PDT, SE);

  BcToInstrument.swap(NewBcToInstrument);
  GepToInstrument.swap(NewGepToInstrument);
}

bool ShadowBound::patternMatch(Function &F, Instruction *I,
                                   PatternBase *P) {

  if (P->getType() == PT_VALUE) {
    ValueIdentBase *VI = static_cast<ValuePattern *>(P)->getIdent();

    if (VI->getType() == VIT_FUNARG) {
      FunArgIdent *FAI = static_cast<FunArgIdent *>(VI);

      // A static function is only unique together with its module. A pattern
      // without a module describes an EXTERNAL function, so it must not match a
      // static one; a pattern with a module must match this module's base name
      // exactly. The previous suffix test let an empty module match every
      // static function (endswith("") is always true) and let "x.c" match
      // "prefix-x.c" or a different directory's "x.c".
      if (F.hasLocalLinkage()) {
        if (FAI->getModuleName().empty())
          return false;
        StringRef ThisModule =
            sys::path::filename(F.getParent()->getModuleIdentifier());
        if (ThisModule != sys::path::filename(FAI->getModuleName()))
          return false;
      }
      if (FAI->getName() == F.getName()) {
        if (auto Arg = dyn_cast<Argument>(getSource(I))) {
          if (Arg->getArgNo() == FAI->getIndex()) {
            return true;
          }
        }
      }
    } else if (VI->getType() == VIT_STRUCT) {
      StructMemberIdent *SI = static_cast<StructMemberIdent *>(VI);
      if (auto *LI = dyn_cast<LoadInst>(getSource(I))) {
        StructMemberIdent *LSI = findStructMember(&F, LI->getPointerOperand());
        if (LSI != nullptr && LSI->getName() == SI->getName() &&
            LSI->getIndex() == SI->getIndex()) {
          return true;
        }
      }
    }
  }

  return false;
}

void ShadowBound::patternOptimize(Function &F) {
  if (ClPatternOptFile == "" || !ClPatternOpt)
    return;

  auto Patterns = parsePatternFile(ClPatternOptFile);
  if (Patterns.empty())
    return;

  auto match = [&](Instruction *I) {
    for (auto P : Patterns)
      if (patternMatch(F, I, P))
        return true;
    return false;
  };

  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument;
  SmallVector<BitCastInst *, 16> NewBcToInstrument;

  for (auto *Gep : GepToInstrument)
    if (!match(Gep))
      NewGepToInstrument.push_back(Gep);
  for (auto *Bc : BcToInstrument)
    if (!match(Bc))
      NewBcToInstrument.push_back(Bc);

  GepToInstrument.swap(NewGepToInstrument);
  BcToInstrument.swap(NewBcToInstrument);
}

void ShadowBound::ipoOptimize(Function &F) {
  // Bounds live only in the heap's shadow, and a check on a source outside the
  // heap is skipped at run time (getPointerIsApp). Drop such checks statically
  // when the source can be proven to never be a heap pointer: a stack or global
  // object locally, or an argument / call result via the IPO facts.
  const ShadowBoundIPOInfo NoIPO;
  const ShadowBoundIPOInfo &Info = IPO ? *IPO : NoIPO;
  auto Elide = [&](Instruction *I) {
    Value *Src = getSource(I);
    if (ClNonHeapOpt && Info.isNonHeap(Src)) {
      ++NumNonHeapElided;
      return true;
    }
    if (Info.isTrustedStructField(F, Src)) {
      ++NumStructHeuristicElided;
      return true;
    }
    return false;
  };

  llvm::erase_if(GepToInstrument, Elide);
  llvm::erase_if(BcToInstrument, Elide);
}

void ShadowBound::structPointerOptimizae(Function &F, ScalarEvolution &SE) {
  if (!ClReserveOpt)
    return;

  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument;
  for (auto *GEP : GepToInstrument) {
    if (isAccessMember(GEP))
      continue;
    if (isAccessMemberBoost(GEP, SE))
      continue;
    NewGepToInstrument.push_back(GEP);
  }

  GepToInstrument.swap(NewGepToInstrument);
}

SmallVector<BitCastInst *, 16>
ShadowBound::dependencyOptimizeForBc(Function &F, DominatorTree &DT,
                                         PostDominatorTree &PDT,
                                         ScalarEvolution &SE) {
  SmallVector<BitCastInst *, 16> NewBcToInstrument;

  for (size_t i = 0; i < BcToInstrument.size(); ++i) {
    bool optimized = false;
    for (size_t j = 0; j < BcToInstrument.size(); ++j) {
      if (i != j) {
        auto I = BcToInstrument[i];
        auto J = BcToInstrument[j];
        // Only a check that is guaranteed to run BEFORE I (J dominates I) can
        // make I's check redundant. Post-dominance is unsound: J would run
        // after I has already been dereferenced, so I's access would go
        // unchecked. Dominance also rules out two casts eliminating each
        // other, since only one of them can dominate the other.
        if (DT.dominates(J, I)) {
          if (I->getOperand(0) == J->getOperand(0)) {
            size_t ISize =
                DL->getTypeStoreSize(I->getType()->getPointerElementType());
            size_t JSize =
                DL->getTypeStoreSize(J->getType()->getPointerElementType());
            if (ISize <= JSize) {
              optimized = true;
              ReliedUpon.insert(J);
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
ShadowBound::dependencyOptimizeForGep(Function &F, DominatorTree &DT,
                                          PostDominatorTree &PDT,
                                          ScalarEvolution &SE) {
  // Redundant-check elimination. For each GEP I, find GEPs J on the same base
  // that are guaranteed to run BEFORE I (J dominates I) and whose check already
  // covers one side of I's bounds. Only that side (direction) is dropped; the
  // full check is never removed on the strength of a check that runs later.
  //
  // Soundness, using signed BYTE offsets relative to the shared base:
  //   * if I's offset is always <= J's offset and J checks the upper bound,
  //     then base+I <= base+J <= end, so I's overflow check is redundant;
  //   * if I's offset is always >= J's offset and J checks the lower bound,
  //     then base+I >= base+J >= begin, so I's underflow check is redundant.
  // Both hold transitively even if J's own direction is later dropped, because
  // the dropped side was enforced by a check dominating J (hence dominating I).
  for (size_t i = 0; i < GepToInstrument.size(); ++i) {
    auto *I = GepToInstrument[i];
    Value *Base = I->getPointerOperand();
    const SCEV *OffI = SE.getMinusSCEV(SE.getSCEV(I), SE.getSCEV(Base));
    OffsetDir Drop = kOffsetUnknown;

    for (size_t j = 0; j < GepToInstrument.size() && Drop != kOffsetBoth; ++j) {
      if (i == j)
        continue;
      auto *J = GepToInstrument[j];
      if (J->getPointerOperand() != Base)
        continue;
      if (!DT.dominates(J, I))
        continue;

      setOffsetDir(J, SE);
      OffsetDir JDir = OffsetDirCache[J];
      const SCEV *OffJ = SE.getMinusSCEV(SE.getSCEV(J), SE.getSCEV(Base));

      // OffI <= OffJ always  &&  J checks the upper bound.
      if ((JDir & kOffsetPositive) &&
          SE.getSignedRangeMax(OffI).sle(SE.getSignedRangeMin(OffJ))) {
        Drop |= kOffsetPositive;
        ReliedUpon.insert(J);
      }

      // OffI >= OffJ always  &&  J checks the lower bound.
      if ((JDir & kOffsetNegative) &&
          SE.getSignedRangeMin(OffI).sge(SE.getSignedRangeMax(OffJ))) {
        Drop |= kOffsetNegative;
        ReliedUpon.insert(J);
      }
    }

    DroppedDir[I] = Drop;
  }

  // Direction refinement happens after setOffsetDir in collectChunkCheckImpl;
  // nothing is removed from the work list here.
  return GepToInstrument;
}

Value *ShadowBound::getSource(Value *I) {
  if (SourceCache.count(I))
    return SourceCache[I];

  return SourceCache[I] = getSourceImpl(I);
}

Value *ShadowBound::getSourceImpl(Value *V) {
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

bool ShadowBound::getPhiSource(Value *V, Value *&Src,
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

void ShadowBound::collectChunkCheck(Function &F, LoopInfo &LI,
                                        ObjectSizeOffsetEvaluator &ObjSizeEval,
                                        ScalarEvolution &SE,
                                        DominatorTree &DT) {
  DenseMap<Value *, SmallVector<Instruction *, 16>> SourceMap;

  for (auto &I : GepToInstrument) {
    Value *Source = getSource(I);
    SourceMap[Source].push_back(I);
  }

  for (auto &I : BcToInstrument) {
    Value *Source = getSource(I);
    SourceMap[Source].push_back(I);
  }

  for (auto &[Src, Insts] : SourceMap) {
    ASSERT(Src != nullptr);
    collectChunkCheckImpl(F, Src, Insts, LI, ObjSizeEval, SE, DT);
  }
}

[[maybe_unused]] StructType *ShadowBound::sourceAnalysis(Function &F,
                                                             Value *Src) {
  if (auto *LI = dyn_cast<LoadInst>(Src)) {
    if (auto *Gep = dyn_cast<GetElementPtrInst>(LI->getPointerOperand())) {
      bool isFirstField = true;
      Type *SrcTy = nullptr;
      Type *DstTy = Gep->getSourceElementType();

      if (isFixedSizeType(DstTy)) {
        for (auto &Op : Gep->indices()) {
          if (isFirstField) {
            isFirstField = false;
            continue;
          }

          auto value = Op.get();
          if (value->getType()->isIntegerTy(32)) {
            StructType *STy = cast<StructType>(DstTy);
            auto index = cast<ConstantInt>(value)->getZExtValue();
            SrcTy = DstTy;
            DstTy = STy->getElementType(index);
          } else {
            auto Aty = cast<ArrayType>(DstTy);
            SrcTy = DstTy;
            DstTy = Aty->getArrayElementType();
          }
        }

        ASSERT(DstTy == Src->getType());
        if (SrcTy->isStructTy()) {
          return cast<StructType>(SrcTy);
        }
      }
    }
  }

  if (isa<Argument>(Src)) {
    if (auto *STy = dyn_cast<StructType>(Src->getType())) {
      return STy;
    }
  }

  return nullptr;
}

void ShadowBound::setOffsetDir(Value *Addr, ScalarEvolution &SE) {
  if (!ClDirectionOpt)
    OffsetDirCache[Addr] = kOffsetBoth;

  if (OffsetDirCache.count(Addr))
    return;

  Value *Src = getSource(Addr);
  OffsetDir Dir = kOffsetUnknown;

  SmallVector<Value *, 16> WorkList;
  SmallPtrSet<Value *, 16> Visited;

  WorkList.push_back(Addr);
  while (!WorkList.empty()) {
    Value *V = WorkList.pop_back_val();
    if (Visited.count(V))
      continue;

    Visited.insert(V);
    if (OffsetDirCache.count(V)) {
      Dir |= OffsetDirCache[V];
      if (Dir == kOffsetBoth)
        break;
      continue;
    }

    if (auto *BC = dyn_cast<BitCastInst>(V)) {
      WorkList.push_back(BC->getOperand(0));
    } else if (auto *Phi = dyn_cast<PHINode>(V)) {
      if (Phi != Src) {
        for (size_t i = 0; i < Phi->getNumIncomingValues(); ++i)
          WorkList.push_back(Phi->getIncomingValue(i));
      }
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
      // Use the sign of the actual BYTE offset of this GEP step, not the sign
      // of the raw indices: an index can be non-negative yet, once scaled by
      // the element size, wrap to a negative byte offset. The pointer-
      // difference SCEV accounts for the scaling, and its range becomes the
      // full range (hence kOffsetBoth) when a wrap cannot be ruled out.
      const SCEV *Off = SE.getMinusSCEV(SE.getSCEV(GEP),
                                        SE.getSCEV(GEP->getPointerOperand()));
      ConstantRange R = SE.getSignedRange(Off);
      if (R.getSignedMin().isNonNegative())
        Dir |= kOffsetPositive;
      else if (R.getSignedMax().isNegative())
        Dir |= kOffsetNegative;
      else
        Dir |= kOffsetBoth;

      if (Dir == kOffsetBoth)
        break;

      WorkList.push_back(GEP->getPointerOperand());
    }
  }

  OffsetDirCache[Addr] = Dir;
}

OffsetDir ShadowBound::getOffsetDir(Value *Addr) {
  ASSERT(OffsetDirCache.count(Addr));

  return OffsetDirCache[Addr];
}

bool ShadowBound::isAccessMember(Instruction *I) {
  ASSERT(isa<GetElementPtrInst>(I));
  auto *GEP = cast<GetElementPtrInst>(I);
  Type *SrcTy = GEP->getSourceElementType();
  if (!isFixedSizeType(SrcTy))
    return false;

  // Only drop the check when the whole access is at a compile-time-known
  // offset that stays inside the object the struct/array pointer describes.
  // A variable index into an inner array (`s->arr[i]`) can leave the object
  // entirely and must still be checked.
  if (!GEP->hasAllConstantIndices())
    return false;

  auto *First = dyn_cast<ConstantInt>(GEP->getOperand(1));
  if (!First || !First->isZero())
    return false;

  APInt Offset(DL->getIndexSizeInBits(GEP->getPointerAddressSpace()), 0, true);
  if (!GEP->accumulateConstantOffset(*DL, Offset) || Offset.isNegative())
    return false;

  uint64_t Need = DL->getTypeStoreSize(GEP->getType()->getPointerElementType());
  return Offset.getZExtValue() + Need <= DL->getTypeStoreSize(SrcTy);
}

bool ShadowBound::isAccessMemberBoost(Instruction *I, ScalarEvolution &SE) {
  // Optimize the following case:
  //  Obj* obj = ...
  //  u8* buf = (u8*) obj;
  //  buf[1] = 0;
  //
  // Only sound when the offset from the object base is a compile-time constant
  // (so no variable index can leave the object) and, measured in BYTES, the
  // access stays within sizeof(*obj). The previous code summed raw index
  // values (element counts, not bytes) into a 64-bit accumulator that could
  // wrap, so a huge or negative index was wrongly treated as in bounds.
  Value *Src = getSource(I);

  ASSERT(Src->getType()->isPointerTy());
  auto *SrcPtrTy = cast<PointerType>(Src->getType());
  Type *ty = SrcPtrTy->getPointerElementType();

  if (!isFixedSizeType(ty))
    return false;

  uint64_t size = getFixedSize(ty, DL);
  APInt Offset(DL->getIndexSizeInBits(SrcPtrTy->getAddressSpace()), 0, true);

  Value *V = I;
  while (V != Src) {
    if (auto *BC = dyn_cast<BitCastInst>(V))
      V = BC->getOperand(0);
    else if (auto *BCO = dyn_cast<BitCastOperator>(V))
      V = BCO->getOperand(0);
    else if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      // accumulateConstantOffset adds this GEP's byte offset and fails on any
      // non-constant index.
      if (!GEP->accumulateConstantOffset(*DL, Offset))
        return false;
      V = GEP->getPointerOperand();
    } else {
      return false;
    }
  }

  if (Offset.isNegative())
    return false;

  uint64_t Need = DL->getTypeStoreSize(I->getType()->getPointerElementType());
  return Offset.getZExtValue() + Need <= size;
}

void ShadowBound::collectChunkCheckImpl(
    Function &F, Value *Src, SmallVector<Instruction *, 16> &Insts,
    LoopInfo &LI, ObjectSizeOffsetEvaluator &ObjSizeEval, ScalarEvolution &SE,
    DominatorTree &DT) {
  if (tryRuntimeFreeCheck(F, Src, Insts, ObjSizeEval)) {
    return;
  }

  // Compute each instruction's direction, then subtract any direction a
  // dominating check already covers (redundant-check elimination). An
  // instruction left with no direction to check is dropped entirely.
  SmallVector<Instruction *, 16> Kept;
  for (auto *I : Insts) {
    setOffsetDir(I, SE);
    // Bitcasts do not carry a direction (their check is an access-size check,
    // handled separately), so only GEP direction is refined and filtered.
    if (isa<GetElementPtrInst>(I)) {
      auto It = DroppedDir.find(I);
      if (It != DroppedDir.end())
        OffsetDirCache[I] = OffsetDirCache[I] & ~It->second;
      if (OffsetDirCache[I] == kOffsetUnknown)
        continue;
    }
    Kept.push_back(I);
  }
  Insts.swap(Kept);

  if (Insts.empty())
    return;

  if (!ClMergeOpt) {
    Checks.push_back(new RuntimeCheck(Src, Insts));
    return;
  }

#if 1
  int weight = 0;

  for (auto *I : Insts)
    weight += LI.getLoopFor(I->getParent()) != nullptr ? 5 : 1;

  if (weight <= 2) {
    Checks.push_back(new RuntimeCheck(Src, Insts));
  } else {
    Instruction *InsertPt =
        isa<Instruction>(Src)
            ? cast<Instruction>(Src)->getInsertionPointAfterDef()
            : entryInsertPtAfterAllocas(F);
    Checks.push_back(new ClusterCheck(Src, InsertPt, Insts));
  }
#else
  DenseMap<Instruction *, Instruction *> Parent;
  DenseMap<Instruction *, Instruction *> CheckPt;
  std::function<Instruction *(Instruction *)> find;
  find = [&](Instruction *V) {
    return Parent[V] == V ? V : Parent[V] = find(Parent[V]);
  };
  auto merge = [&](Instruction *U, Instruction *V) {
    CheckPt[find(V)] =
        DT.findNearestCommonDominator(CheckPt[find(U)], CheckPt[find(V)]);
    return Parent[find(U)] = find(V);
  };

  for (auto *I : Insts) {
    Parent[I] = I;
    CheckPt[I] = I;
  }

  // We first split the instructions into different groups
  // If two instructions are in the different group, then
  // they are not reachable from each other.
  // For each group, we will insert bound-fetch code at the
  // Dominator LCA of all instructions in the group.
  for (auto *X : Insts)
    for (auto *Y : Insts)
      if (find(X) != find(Y) && isPotentiallyReachable(X, Y))
        merge(X, Y);

  BasicBlock *SrcBlock = isa<Argument>(Src)
                             ? &F.getEntryBlock()
                             : cast<Instruction>(Src)->getParent();

  bool flag;
  do {
    flag = false;
    for (auto *X : Insts) {
      for (auto *Y : Insts) {
        auto XB = CheckPt[find(X)]->getParent();
        auto YB = CheckPt[find(Y)]->getParent();

        // Two groups' checkpoint has dominated relationship,
        // We better merge them into same group. (not sure if it is correct)
        if (find(X) != find(Y) &&
            (DT.dominates(XB, YB) || DT.dominates(YB, XB))) {
          flag = true;
          merge(X, Y);
        }
      }

      // If the checkpoint of X is in the loop, then we try
      // move the checkpoint to the outside of the loop.
      if (LI.getLoopFor(CheckPt[find(X)]->getParent()) != nullptr) {
        if (auto *IDom = DT.getNode(CheckPt[find(X)]->getParent())->getIDom()) {
          auto *BB = IDom->getBlock();
          if (BB != nullptr && DT.dominates(SrcBlock, BB)) {
            flag = true;
            CheckPt[find(X)] = BB->getTerminator();
          }
        }
      }
    }
  } while (flag);

  // =============== Debug Section ===============
  for (auto *I : Insts)
    ASSERT(DT.dominates(SrcBlock, CheckPt[find(I)]->getParent()));
  // =============================================

  SmallPtrSet<Instruction *, 16> Visited;
  SmallVector<Instruction *, 16> GroupInsts;

  for (auto *X : Insts) {
    if (Visited.count(X))
      continue;

    GroupInsts.clear();

    int weight = 0;
    for (auto *Y : Insts) {
      if (find(X) == find(Y)) {
        Visited.insert(Y);
        GroupInsts.push_back(Y);
        weight += LI.getLoopFor(Y->getParent()) != nullptr ? 5 : 1;
      }
    }

    if (weight <= 2) {
      Checks.push_back(new RuntimeCheck(Src, GroupInsts));
    } else {
      Checks.push_back(new ClusterCheck(Src, CheckPt[find(X)], GroupInsts));
    }
  }
#endif
}

// Replace the per-iteration check of a monotonic induction pointer with a single
// bound check before the loop. Sound because the accessed addresses of an affine
// add-recurrence are monotonic, so they all lie in [min, max] of the first and
// last iteration's addresses; bounding those two covers every iteration. Returns
// true (and drops the per-iteration check) only when SCEV can prove the range.
bool ShadowBound::monotonicLoopOptimize(Function &F, Value *Addr, Loop *Lop,
                                        ScalarEvolution &SE, DominatorTree &DT) {
  auto *GEP = dyn_cast<GetElementPtrInst>(Addr);
  if (!GEP)
    return false;

  // Only the one-directional case fits a single monotonic range cleanly.
  // (Directions are not computed until collectChunkCheck, so compute it here.)
  setOffsetDir(GEP, SE);
  OffsetDir Dir = getOffsetDir(GEP);
  if (Dir != kOffsetPositive && Dir != kOffsetNegative)
    return false;

  auto *ARE = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(Addr));
  if (!ARE || ARE->getLoop() != Lop || !ARE->isAffine())
    return false;

  const SCEV *BTC = SE.getBackedgeTakenCount(Lop);
  if (isa<SCEVCouldNotCompute>(BTC))
    return false;
  BasicBlock *PH = Lop->getLoopPreheader();
  if (!PH)
    return false;

  // Shadow bounds come from the (loop-invariant) source pointer; it must be
  // available in the preheader.
  Value *Src = getSource(Addr);
  if (auto *SrcI = dyn_cast<Instruction>(Src))
    if (!DT.dominates(SrcI, PH->getTerminator()))
      return false;

  const SCEV *Start = ARE->getStart();
  const SCEV *Last = ARE->evaluateAtIteration(BTC, SE);
  ConstantRange StepR = SE.getSignedRange(ARE->getStepRecurrence(SE));

  const SCEV *MinS, *MaxS;
  if (StepR.getSignedMin().isNonNegative()) {
    MinS = Start;
    MaxS = Last;
  } else if (StepR.getSignedMax().isNegative()) {
    MinS = Last;
    MaxS = Start;
  } else {
    return false; // step sign not provable
  }

  Instruction *IP = PH->getTerminator();
  SCEVExpander Exp(SE, *DL, "shadowboundbound");
  if (!Exp.isSafeToExpandAt(MinS, IP) || !Exp.isSafeToExpandAt(MaxS, IP))
    return false;

  Value *MinP = Exp.expandCodeFor(MinS, Addr->getType(), IP);
  Value *MaxP = Exp.expandCodeFor(MaxS, Addr->getType(), IP);

  BuilderTy IRB(IP->getParent(), IP->getIterator(), TargetFolder(*DL));
  Value *SrcInt = IRB.CreatePtrToInt(Src, int64Type);
  Value *MinInt = IRB.CreatePtrToInt(MinP, int64Type);
  Value *MaxInt = IRB.CreatePtrToInt(MaxP, int64Type);

  // Guard on the source being a heap pointer, then fetch its bounds once and
  // bound both extremes of the accessed range.
  Value *IsApp = getPointerIsApp(SrcInt, IRB);
  IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, IP, false));

  uint64_t NeededSize =
      DL->getTypeStoreSize(GEP->getType()->getPointerElementType());
  Value *Begin = nullptr, *End = nullptr;
  getPointerBeginEnd(SrcInt, Begin, End, IRB);
  Value *CmpLo = IRB.CreateICmpULT(MinInt, Begin);
  Value *CmpHi = makeOverflowCmp(IRB, MaxInt, End, NeededSize);
  CreateTrapBB(IRB, IRB.CreateOr(CmpLo, CmpHi), true);

  Counter[kClusterCheck]++;
  ++NumClusterChecks;
  ++NumRuntimeChecks;
  return true;
}

void ShadowBound::loopOptimize(Function &F, LoopInfo &LI,
                                   ScalarEvolution &SE, DominatorTree &DT,
                                   PostDominatorTree &PDT) {
  if (!ClLoopOpt)
    return;

  collectMonoLoop(F, LI, SE);

  SmallPtrSet<GetElementPtrInst *, 16> GepSet(GepToInstrument.begin(),
                                              GepToInstrument.end());
  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument;
  for (auto *GEP : GepToInstrument) {
    Loop *Loop = LI.getLoopFor(GEP->getParent());
    if (MonoLoopMap.count(Loop) != 0) {
      // monotonicLoopOptimize() is a no-op stub: it inserts no replacement
      // check, so a GEP may only be dropped here if that function actually
      // optimized it. The previous code unconditionally dropped the induction
      // step GEP whenever the loop bound was not itself a GEP, leaving the
      // loop body with no bounds check at all.
      if (monotonicLoopOptimize(F, GEP, Loop, SE, DT))
        continue;
    }

    NewGepToInstrument.push_back(GEP);
  }

  GepToInstrument.swap(NewGepToInstrument);
}

void ShadowBound::collectMonoLoop(Function &F, LoopInfo &LI,
                                      ScalarEvolution &SE) {
  for (auto *Loop : LI) {
    if (!Loop->isRotatedForm())
      continue;

    auto *Preheader = Loop->getLoopPreheader(); // nullptr possible
    auto *Header = Loop->getHeader();
    auto *ExitCmp = Loop->getLatchCmpInst();

    if (Header == nullptr || ExitCmp == nullptr)
      continue;

    auto *Latch = Loop->getLoopLatch();

    // Find the possible guard block
    BasicBlock *GuardBB = nullptr;
    if (Preheader != nullptr)
      GuardBB = Preheader->getUniquePredecessor();
    else {
      SmallVector<BasicBlock *, 4> GuardBlocks;
      for (auto *Pred : predecessors(Header)) {
        if (Pred == Latch)
          continue;
        GuardBlocks.push_back(Pred);
      }
      if (GuardBlocks.size() == 1)
        GuardBB = GuardBlocks.front();
    }

    if (GuardBB == nullptr)
      continue;

    // Find the possible exit block
    SmallVector<BasicBlock *, 4> GuardExitBlocks;
    SmallVector<BasicBlock *, 4> LatchExitBlocks;
    for (auto *Succ : successors(GuardBB)) {
      if (Succ == Header || Succ == Preheader)
        continue;
      GuardExitBlocks.push_back(Succ);
    }
    for (auto *Succ : successors(Latch)) {
      if (Succ == Header)
        continue;
      LatchExitBlocks.push_back(Succ);
    }

    if (GuardExitBlocks.size() != 1 || LatchExitBlocks.size() != 1)
      continue;

    auto *ExitBB = GuardExitBlocks.front();
    auto *LatchExitBB = LatchExitBlocks.front();

    if (LatchExitBB != ExitBB &&
        (LatchExitBB->getUniqueSuccessor() == nullptr ||
         LatchExitBB->getUniqueSuccessor()->sizeWithoutDebug() != 1 ||
         LatchExitBB->getUniqueSuccessor()->getUniqueSuccessor() != ExitBB))
      continue;

    auto *GuardBranchInst = dyn_cast<BranchInst>(GuardBB->getTerminator());
    if (GuardBranchInst == nullptr || GuardBranchInst->isUnconditional() ||
        (GuardBranchInst->getSuccessor(0) != Header &&
         GuardBranchInst->getSuccessor(0) != Preheader))
      continue;

    auto *GuardCmp = dyn_cast<ICmpInst>(GuardBranchInst->getCondition());
    if (GuardCmp == nullptr)
      continue;

    auto *IndVar = Loop->getInductionVariableBoost(SE, GuardBB);
    if (IndVar == nullptr)
      continue;

    auto *Enter = Preheader != nullptr ? Preheader : GuardBB;

    Value *Step = IndVar->getIncomingValueForBlock(Latch);
    Value *ExitCmpOp0 = ExitCmp->getOperand(0);
    Value *ExitCmpOp1 = ExitCmp->getOperand(1);

    Value *Lower = IndVar->getIncomingValueForBlock(Enter);
    Value *Upper = nullptr;

    if (ExitCmpOp0 == Step ||
        (isa<CastInst>(ExitCmpOp0) &&
         cast<CastInst>(ExitCmpOp0)->getOperand(0) == Step))
      Upper = ExitCmpOp1;

    if (ExitCmpOp1 == Step ||
        (isa<CastInst>(ExitCmpOp1) &&
         cast<CastInst>(ExitCmpOp1)->getOperand(0) == Step))
      Upper = ExitCmpOp0;

    if (Upper == nullptr)
      continue;

    while (!Loop->isLoopInvariant(Upper) && isa<CastInst>(Upper))
      Upper = cast<CastInst>(Upper)->getOperand(0);

    if (!Loop->isLoopInvariant(Upper))
      continue;
    ASSERT(Loop->isLoopInvariant(Lower));

    LLVM_DEBUG(dbgs() << "[Mono Loop]\n");
    LLVM_DEBUG(dbgs() << "IndVar: " << *IndVar << "\n");
    LLVM_DEBUG(dbgs() << "Lower: " << *Lower << "\n");
    LLVM_DEBUG(dbgs() << "Upper: " << *Upper << "\n");
    LLVM_DEBUG(dbgs() << "Step: " << *Step << "\n");
    LLVM_DEBUG(dbgs() << "StepInst: " << *IndVar->getIncomingValueForBlock(Latch) << "\n");
    LLVM_DEBUG(dbgs() << "GuardCond: " << *GuardCmp << "\n");
    LLVM_DEBUG(dbgs() << "ExitCmp: " << *ExitCmp << "\n");

    MonoLoopMap[Loop] =
        new MonoLoop(Loop, IndVar, Lower, Upper, Step, GuardBB, Preheader);
  }
}

bool ShadowBound::tryRuntimeFreeCheck(
    Function &F, Value *Src, SmallVector<Instruction *, 16> &Insts,
    ObjectSizeOffsetEvaluator &ObjSizeEval) {
  SizeOffsetEvalType SizeOffsetEval = ObjSizeEval.compute(Src);

  if (ObjSizeEval.bothKnown(SizeOffsetEval)) {
    // ShadowBound only protects heap objects. A stack (alloca) or global source
    // has a statically known size but is deliberately left unchecked; a heap
    // allocation (malloc/calloc/realloc/new) is checked against its known size
    // instead of the shadow.
    if (isa<AllocaInst>(Src) || isa<GlobalValue>(Src))
      return true;
    Checks.push_back(new BuiltinCheck(Src, SizeOffsetEval.first,
                                      SizeOffsetEval.second, Insts));
    return true;
  }

  return false;
}

void ShadowBound::instrumentBitCast(Function &F, Value *Src,
                                        BitCastInst *BC) {
  // ShadowAddr = BC & kShadowMask;
  // Base = BC & kShadowBase;
  // BackSize = *(int32_t *) ShadowAddr;
  // if (BC > Base + BackSize - NeededSize)
  //   report_overflow();

  Instruction *InsertPt = getCheckInsertPt(BC);
  SelectInst *Sel = getOnlySelectUser(BC);
  bool OnlyStored = isOnlyStored(BC);

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *CmpPtr = IRB.CreatePtrToInt(BC, int64Type);

  {
    // FIXME: This block can be removed?
    Value *IsApp = getPointerIsApp(Ptr, IRB);
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));
  }

  Value *End = nullptr;
  getPointerEnd(Ptr, End, IRB);

  uint64_t NeededSize =
      DL->getTypeStoreSize(BC->getType()->getPointerElementType());
  ASSERT(NeededSize > kReservedBytes);

  Value *Cmp = makeOverflowCmp(IRB, CmpPtr, End, NeededSize, OnlyStored);
  CreateTrapBB(IRB, guardBySelect(Sel, BC, Cmp, IRB), true);
}

void ShadowBound::instrumentGep(Function &F, Value *Src,
                                    GetElementPtrInst *GEP) {
  // ShadowAddr = GEP & kShadowMask;
  // Base = GEP & kShadowBase;
  // Packed = *(int32_t *) ShadowAddr;
  // Front = Packed & 0xffffffff;
  // Back = Packed >> 32;
  // Begin = Base - (Front << 3);
  // End = Base + (Back << 3);
  // if (GEP < Begin || GEP + NeededSize > End)
  //   report_overflow();

  Instruction *InsertPt = getCheckInsertPt(GEP);
  SelectInst *Sel = getOnlySelectUser(GEP);
  bool OnlyStored = isOnlyStored(GEP);
  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *CmpPtr = IRB.CreatePtrToInt(GEP, int64Type);

  {
    // FIXME: This block can be removed?
    Value *IsApp = getPointerIsApp(Ptr, IRB);
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));
  }

  Value *Begin = nullptr;
  Value *End = nullptr;
  Value *Cmp = nullptr;

  if (getOffsetDir(GEP) == kOffsetBoth) {
    getPointerBeginEnd(Ptr, Begin, End, IRB);
    Value *CmpBegin = IRB.CreateICmpULT(CmpPtr, Begin);

    uint64_t NeededSize =
        DL->getTypeStoreSize(GEP->getType()->getPointerElementType());
    Value *CmpEnd =
        makeOverflowCmp(IRB, CmpPtr, End, NeededSize, OnlyStored);
    Cmp = IRB.CreateOr(CmpBegin, CmpEnd);
  } else if (getOffsetDir(GEP) == kOffsetPositive) {
    getPointerEnd(Ptr, End, IRB);

    uint64_t NeededSize =
        DL->getTypeStoreSize(GEP->getType()->getPointerElementType());
    Cmp = makeOverflowCmp(IRB, CmpPtr, End, NeededSize, OnlyStored);
  } else if (getOffsetDir(GEP) == kOffsetNegative) {
    getPointerBegin(Ptr, Begin, IRB);

    Cmp = IRB.CreateICmpULT(CmpPtr, Begin);
  }

  ASSERT(Cmp != nullptr);
  CreateTrapBB(IRB, guardBySelect(Sel, GEP, Cmp, IRB), true);
}

// True if I's value is only written to memory or returned, never accessed or
// used to derive another pointer here. Must be asked before the check adds
// its own users of I.
bool ShadowBound::isOnlyStored(Instruction *I) {
  if (I->use_empty())
    return false;
  for (User *U : I->users()) {
    if (auto *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getValueOperand() != I || SI->getPointerOperand() == I)
        return false;
    } else if (!isa<ReturnInst>(U)) {
      return false;
    }
  }
  return true;
}

Value *ShadowBound::makeOverflowCmp(BuilderTy &IRB, Value *CmpPtr,
                                        Value *End, uint64_t NeededSize,
                                        bool OnlyStored) {
  // A pointer that is only stored or returned is not accessed here: it only
  // has to be at most one past the end. Requiring room for a whole element
  // rejected std::vector's `++_M_finish` when the vector became full with
  // elements larger than kReservedBytes (SPEC 520.omnetpp_r, 96-byte
  // InifileReader::KeyValue1). Where it is loaded and used later, the loaded
  // pointer is a new source and is checked against its own bounds there.
  if (OnlyStored)
    return IRB.CreateICmpUGT(CmpPtr, End);

  // An N-byte access at CmpPtr is in bounds only if CmpPtr + N <= End, so the
  // violation condition is CmpPtr + N > End. (The old code used CmpPtr > End,
  // which let a pointer sit exactly at the end of the chunk and then read the
  // next one.) When the reserved-bytes optimization is on, checked pointers
  // must keep kReservedBytes of headroom as well, so that the small accesses
  // elided by isZeroAccessGep / isShrinkBitCast stay within the reserve.
  uint64_t Adj = NeededSize;
  if (ClReserveOpt)
    Adj = std::max<uint64_t>(Adj, (uint64_t)kReservedBytes);
  Value *AdjPtr = IRB.CreateAdd(CmpPtr, ConstantInt::get(int64Type, Adj));
  return IRB.CreateICmpUGT(AdjPtr, End);
}

void ShadowBound::getPointerBeginEnd(Value *Ptr, Value *&Begin, Value *&End,
                                         BuilderTy &IRB) {
  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *Packed =
      IRB.CreateLoad(int64Type, IRB.CreateIntToPtr(Shadow, int64PtrType));
  Value *BackRaw =
      IRB.CreateAnd(Packed, ConstantInt::get(int64Type, 0xffffffff));
  Value *FrontRaw = IRB.CreateLShr(Packed, 32);
  Value *Back = ClOnlySmallAllocOpt ? BackRaw : IRB.CreateShl(BackRaw, 3);
  Value *Front = ClOnlySmallAllocOpt ? FrontRaw : IRB.CreateShl(FrontRaw, 3);

  Begin = IRB.CreateSub(Base, Front);
  End = IRB.CreateAdd(Base, Back);
}

void ShadowBound::getPointerEnd(Value *Ptr, Value *&End, BuilderTy &IRB) {
  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *BackRaw = IRB.CreateZExt(
      IRB.CreateLoad(int32Type, IRB.CreateIntToPtr(Shadow, int32PtrType)),
      int64Type);
  Value *Back = ClOnlySmallAllocOpt ? BackRaw : IRB.CreateShl(BackRaw, 3);
  End = IRB.CreateAdd(Base, Back);
}

void ShadowBound::getPointerBegin(Value *Ptr, Value *&Begin,
                                      BuilderTy &IRB) {
  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *ShadowP = IRB.CreateAdd(Shadow, ConstantInt::get(int64Type, 4));
  Value *FrontRaw = IRB.CreateZExt(
      IRB.CreateLoad(int32Type, IRB.CreateIntToPtr(ShadowP, int32PtrType)),
      int64Type);
  Value *Front = ClOnlySmallAllocOpt ? FrontRaw : IRB.CreateShl(FrontRaw, 3);
  Begin = IRB.CreateSub(Base, Front);
}

Value *ShadowBound::getPointerIsApp(Value *Ptr, BuilderTy &IRB) {
  return IRB.CreateAnd(
      IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kHeapSpaceBeg)),
      IRB.CreateICmpULT(Ptr, ConstantInt::get(int64Type, kHeapSpaceEnd)));
}

void ShadowBound::CreateTrapBB(BuilderTy &IRB, Value *Cond, bool Abort) {
  if (Abort && !Options.Recover) {
    MDBuilder MDB(IRB.getContext());
    MDNode *BranchWeights = MDB.createBranchWeights(1, 0);
    // Set the branch weight to 1:0 to tell the compiler
    // that the abort branch is unlikely to be taken.
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(Cond, &*IRB.GetInsertPoint(),
                                                 true, BranchWeights));

    IRB.CreateCall(AbortFn);
  } else {
    IRB.SetInsertPoint(
        SplitBlockAndInsertIfThen(Cond, &*IRB.GetInsertPoint(), false));

    IRB.CreateCall(ReportFn);
  }
}

// Drop the check on a pointer whose only users are GEPs that are checked
// themselves. Its value is never dereferenced or escapes, and every
// pointer derived from it is checked against the same source object (with the
// directions of all steps on the way, this one included), so its own check
// adds nothing. It is also the one that misfires: optimizations reassociate
// `xff + len - 1` into `(xff - 1) + len`, and the intermediate `xff - 1` (a GEP
// without `inbounds`, legally out of bounds) sits before the object when `xff`
// is its first byte. nginx's X-Forwarded-For parser aborted on exactly this.
//
// A pointer stays checked if any user is not in the final checked set: the
// other filters drop a derived pointer's check precisely because its operand
// is checked. Checks other checks were dropped in favour of stay too.
void ShadowBound::dropIntermediateChecks() {
  if (!ClIntermediateOpt)
    return;

  SmallPtrSet<Instruction *, 32> Checked;
  for (auto *C : Checks) {
    if (C->Type == kClusterCheck)
      Checked.insert(((ClusterCheck *)C)->Insts.begin(),
                     ((ClusterCheck *)C)->Insts.end());
    else if (C->Type == kRuntimeCheck)
      Checked.insert(((RuntimeCheck *)C)->Insts.begin(),
                     ((RuntimeCheck *)C)->Insts.end());
    else
      Checked.insert(((BuiltinCheck *)C)->Insts.begin(),
                     ((BuiltinCheck *)C)->Insts.end());
  }

  auto IsIntermediate = [&](Instruction *I) {
    if (I->use_empty() || ReliedUpon.count(I))
      return false;
    // Only GEP users: a GEP's check covers every direction its offset path
    // from the source can take (this step's included), whereas a bitcast's
    // check is an access-size check on the upper bound only.
    for (User *U : I->users()) {
      auto *UI = dyn_cast<GetElementPtrInst>(U);
      if (!UI || !Checked.count(UI))
        return false;
    }
    ++NumIntermediateElided;
    return true;
  };

  for (auto *C : Checks) {
    if (C->Type == kClusterCheck)
      llvm::erase_if(((ClusterCheck *)C)->Insts, IsIntermediate);
    else if (C->Type == kRuntimeCheck)
      llvm::erase_if(((RuntimeCheck *)C)->Insts, IsIntermediate);
    else
      llvm::erase_if(((BuiltinCheck *)C)->Insts, IsIntermediate);
  }
}

// Where to check pointer I: where it is used, not where it is computed.
// Optimizations hoist address computations above the condition guarding their
// use; x264's `pl ? MbQ->qpc[pl-1] : MbQ->qp` computes &qpc[-1] (pl-1 zero-
// extended, 16 GB past the object) before testing pl, and checking it there
// aborted a program that never touches it. So the check goes before the first
// user when all users are in one block, unless that block is in a deeper loop
// (checking once outside the loop is cheaper; the definition is kept then).
Instruction *ShadowBound::getCheckInsertPt(Instruction *I) {
  // A pointer that only feeds a select is used where the select's result is
  // (see guardBySelect).
  if (SelectInst *S = getOnlySelectUser(I))
    return getCheckInsertPt(S);

  Instruction *Def = I->getInsertionPointAfterDef();
  BasicBlock *UseBB = nullptr;
  for (User *U : I->users()) {
    auto *UI = dyn_cast<Instruction>(U);
    if (!UI || isa<PHINode>(UI) || (UseBB && UI->getParent() != UseBB))
      return Def;
    UseBB = UI->getParent();
  }
  if (!UseBB || UseBB == I->getParent())
    return I->hasOneUser() ? I->user_back() : Def;
  if (LoopI && LoopI->getLoopDepth(UseBB) > LoopI->getLoopDepth(I->getParent()))
    return Def;
  for (Instruction &X : *UseBB)
    if (X.getOperandList() && llvm::is_contained(X.operands(), I))
      return &X;
  return Def;
}

// The single user of I if it is a select choosing between I and another
// pointer. Optimizations turn `c ? *a : *b` into `*(c ? a : b)` and compute
// both addresses unconditionally (x264 again: `cmove` between &qp and the
// out-of-object &qpc[pl-1]).
SelectInst *ShadowBound::getOnlySelectUser(Instruction *I) {
  if (!I->hasOneUser())
    return nullptr;
  auto *S = dyn_cast<SelectInst>(I->user_back());
  if (!S || S->getCondition() == I)
    return nullptr;
  return S;
}

// Restrict a check on I to the case where the select it feeds picked it: the
// other arm's address is never used, so it need not be in bounds. S is
// getOnlySelectUser(I), taken before the check's own instructions add users.
Value *ShadowBound::guardBySelect(SelectInst *S, Instruction *I, Value *Cmp,
                                  BuilderTy &IRB) {
  if (!S)
    return Cmp;
  return IRB.CreateAnd(Cmp, IRB.CreateICmpEQ(S, I));
}

void ShadowBound::commitInstrument(Function &F) {
  for (auto *C_ : Checks) {
    BaseCheck &C = *C_;
    if (C.Type == kBuiltInCheck) {
      if (!((BuiltinCheck &)C).Insts.empty())
        commitBuiltInCheck(F, (BuiltinCheck &)C);
    } else if (C.Type == kClusterCheck) {
      if (!((ClusterCheck &)C).Insts.empty())
        commitClusterCheck(F, (ClusterCheck &)C);
    } else if (C.Type == kRuntimeCheck) {
      commitRuntimeCheck(F, (RuntimeCheck &)C);
    } else {
      ASSERT(false);
      __builtin_unreachable();
    }
  }
}

void ShadowBound::commitBuiltInCheck(Function &F, BuiltinCheck &BC) {
  ASSERT(BC.Type == kBuiltInCheck);

  // Only heap objects are checked (stack/global sources never reach here).
  if (!ClCheckHeap)
    return;

  Counter[kBuiltInCheck]++;
  NumBuiltinChecks += BC.Insts.size();

  Value *Src = BC.Src;
  Instruction *InsertPt =
      isa<Instruction>(Src)
          ? cast<Instruction>(Src)->getInsertionPointAfterDef()
          : entryInsertPtAfterAllocas(F);

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Size = BC.Size;
  Value *Offset = BC.Offset;

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  // Object base = Src - Offset; object end = base + Size. The allocator places
  // kReservedBytes after every heap object, which the shadow bounds include,
  // so the end used here includes them too. Checks run when a pointer is
  // created, not when it is dereferenced: comparing against the exact end
  // rejected the legal one-past-the-end pointer of a loop like
  // `p = malloc(n); for (...) p += len;` once LTO inlined the allocation next
  // to it (nginx's ngx_init_setproctitle). This matches the shadow check.
  Value *ObjBegin = IRB.CreateSub(Ptr, Offset);
  Value *PtrEnd = IRB.CreateAdd(IRB.CreateAdd(ObjBegin, Size),
                                ConstantInt::get(int64Type, kReservedBytes));
  // Likewise the front reserve, which the shadow's lower bound includes.
  Value *PtrBegin = IRB.CreateSub(
      ObjBegin, ConstantInt::get(int64Type, kFrontReservedBytes));

  for (auto &I : BC.Insts) {
    IRB.SetInsertPoint(getCheckInsertPt(I));
    SelectInst *Sel = getOnlySelectUser(I);
    bool OnlyStored = isOnlyStored(I);

    uint64_t NeededSize =
        DL->getTypeStoreSize(I->getType()->getPointerElementType());

    Value *Addr = IRB.CreatePtrToInt(I, int64Type);
    Value *CmpBegin = IRB.CreateICmpULT(Addr, PtrBegin);
    // Same upper-bound rule as the shadow check, access width included.
    Value *CmpEnd =
        makeOverflowCmp(IRB, Addr, PtrEnd, NeededSize, OnlyStored);
    Value *Cmp = IRB.CreateOr(CmpBegin, CmpEnd);

    CreateTrapBB(IRB, guardBySelect(Sel, I, Cmp, IRB), true);
  }
}

void ShadowBound::commitClusterCheck(Function &F, ClusterCheck &CC) {
  if (!ClCheckHeap)
    return;

  ASSERT(CC.Type == kClusterCheck);
  Counter[kClusterCheck]++;
  ++NumClusterChecks;
  NumRuntimeChecks += CC.Insts.size();

  Value *Src = CC.Src;
  Instruction *InsertPt = CC.InsertPt;

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  // Shadow = Ptr & kShadowMask;
  // Base = Ptr & kShadowBase;
  // Packed = *(int64_t *) Shadow;
  // Back = Packed & 0xffffffff;
  // Front = Packed >> 32;
  // Begin = Base - (Front << 3);
  // End = Base + (Back << 3);

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  BasicBlock *Head = IRB.GetInsertBlock();

  {
    // FIXME: This block can be removed?
    Value *IsApp = getPointerIsApp(Ptr, IRB);
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));
  }

  OffsetDir DirOr = kOffsetUnknown;
  for (auto *I : CC.Insts) {
    // A bitcast has no offset direction; its check is an access-size (upper)
    // check, so it needs End. Treating it as kOffsetUnknown made the cluster
    // emit no check at all for it.
    if (isa<BitCastInst>(I))
      DirOr |= kOffsetPositive;
    else
      DirOr |= getOffsetDir(I);
  }

  BasicBlock *Then = IRB.GetInsertBlock();

  Value *ThenBegin = nullptr;
  Value *ThenEnd = nullptr;
  PHINode *Begin = nullptr;
  PHINode *End = nullptr;

  if (DirOr == kOffsetBoth)
    getPointerBeginEnd(Ptr, ThenBegin, ThenEnd, IRB);
  else if (DirOr == kOffsetPositive)
    getPointerEnd(Ptr, ThenEnd, IRB);
  else if (DirOr == kOffsetNegative)
    getPointerBegin(Ptr, ThenBegin, IRB);

  IRB.SetInsertPoint(InsertPt);
  if (ThenBegin != nullptr) {
    Begin = IRB.CreatePHI(int64Type, 2);
    Begin->addIncoming(ThenBegin, Then);
    Begin->addIncoming(ConstantInt::get(int64Type, 0), Head);
  }

  if (ThenEnd != nullptr) {
    End = IRB.CreatePHI(int64Type, 2);
    End->addIncoming(ThenEnd, Then);
    // Non-heap fallback: an all-ones End makes the upper check never fire.
    // The previous fallback (kMaxAddress = 2^48) wrongly reported any pointer
    // at or above 2^48 that was derived from non-heap memory.
    End->addIncoming(ConstantInt::get(int64Type, ~0ULL), Head);
  }

  for (auto *I : CC.Insts) {
    IRB.SetInsertPoint(getCheckInsertPt(I));
    SelectInst *Sel = getOnlySelectUser(I);
    bool OnlyStored = isOnlyStored(I);
    Value *Ptr = IRB.CreatePtrToInt(I, int64Type);
    uint64_t NeededSize =
        DL->getTypeStoreSize(I->getType()->getPointerElementType());

    // A bitcast is an access-size (upper) check; a GEP checks the direction(s)
    // its offset can take.
    bool NeedUpper = isa<BitCastInst>(I) || (getOffsetDir(I) & kOffsetPositive);
    bool NeedLower = !isa<BitCastInst>(I) && (getOffsetDir(I) & kOffsetNegative);

    Value *UpperCmp = NeedUpper
                          ? makeOverflowCmp(IRB, Ptr, End, NeededSize,
                                            OnlyStored)
                          : ConstantInt::getFalse(IRB.getContext());
    Value *LowerCmp = NeedLower ? IRB.CreateICmpULT(Ptr, Begin)
                                : ConstantInt::getFalse(IRB.getContext());
    Value *NotIn = IRB.CreateOr(UpperCmp, LowerCmp);
    CreateTrapBB(IRB, guardBySelect(Sel, I, NotIn, IRB), true);
  }
}

void ShadowBound::commitRuntimeCheck(Function &F, RuntimeCheck &RC) {
  if (!ClCheckHeap)
    return;

  ASSERT(RC.Type == kRuntimeCheck);
  Counter[kRuntimeCheck] += RC.Insts.size();
  NumRuntimeChecks += RC.Insts.size();

  Value *Src = RC.Src;

  for (auto *I : RC.Insts) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      instrumentGep(F, Src, GEP);
    } else if (auto *BC = dyn_cast<BitCastInst>(I)) {
      instrumentBitCast(F, Src, BC);
    }
  }
}

[[maybe_unused]] Value *
ShadowBound::readRegister(Function &F, BuilderTy &IRB, StringRef Reg) {
  Module *M = F.getParent();
  Function *readReg = Intrinsic::getDeclaration(M, Intrinsic::read_register,
                                                IRB.getIntPtrTy(*DL));

  LLVMContext &C = M->getContext();
  MDNode *MD = MDNode::get(C, {MDString::get(C, Reg)});
  return IRB.CreateCall(readReg, {MetadataAsValue::get(C, MD)});
}
//===----------------------------------------------------------------------===//
// LTO support
//===----------------------------------------------------------------------===//

PreservedAnalyses ShadowBoundLTOPrepPass::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  if (!hasTypedPointers(M)) {
    M.getContext().emitError(
        "ShadowBound requires typed pointers; compile with "
        "-Xclang -no-opaque-pointers");
    return PreservedAnalyses::all();
  }

  // Max: if any input asks for recovery, the merged module recovers.
  uint32_t Flag = kLTOFlagEnabled | (Options.Recover ? kLTOFlagRecover : 0);
  M.addModuleFlag(Module::Max, kShadowBoundLTOMarker, Flag);

  // Mark the functions of this translation unit, so that at link time code
  // from inputs built without ShadowBound is left alone.
  for (Function &F : M)
    if (!F.isDeclaration())
      F.addFnAttr(kShadowBoundLTOMarker);

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

//===----------------------------------------------------------------------===//
// Interprocedural non-heap analysis
//===----------------------------------------------------------------------===//

AnalysisKey ShadowBoundIPOAnalysis::Key;

bool ShadowBoundIPOInfo::invalidate(Module &, const PreservedAnalyses &PA,
                                    ModuleAnalysisManager::Invalidator &) {
  return !PA.getChecker<ShadowBoundIPOAnalysis>().preservedWhenStateless();
}

bool ShadowBoundIPOInfo::isNonHeap(const Value *V) const {
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 16> Worklist;

  Worklist.push_back(V);
  while (!Worklist.empty()) {
    const Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    // Pointer arithmetic and casts keep the underlying object.
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      Worklist.push_back(GEP->getPointerOperand());
      continue;
    }
    if (auto *BC = dyn_cast<BitCastOperator>(V)) {
      Worklist.push_back(BC->getOperand(0));
      continue;
    }
    if (auto *Phi = dyn_cast<PHINode>(V)) {
      for (const Value *In : Phi->incoming_values())
        Worklist.push_back(In);
      continue;
    }
    if (auto *Sel = dyn_cast<SelectInst>(V)) {
      Worklist.push_back(Sel->getTrueValue());
      Worklist.push_back(Sel->getFalseValue());
      continue;
    }

    // Stack and global objects; null/undef point at no object.
    if (isa<AllocaInst>(V) || isa<GlobalValue>(V) ||
        isa<ConstantPointerNull>(V) || isa<UndefValue>(V))
      continue;

    if (auto *A = dyn_cast<Argument>(V)) {
      if (NonHeapArgs.count(A))
        continue;
      return false;
    }

    if (auto *CB = dyn_cast<CallBase>(V)) {
      // A call that returns one of its arguments (`returned`, launder, ...).
      if (const Value *Ret = getArgumentAliasingToReturnedPointer(CB, false)) {
        Worklist.push_back(Ret);
        continue;
      }
      if (const Function *Callee = CB->getCalledFunction())
        if (NonHeapReturns.count(Callee))
          continue;
      return false;
    }

    // Loads, inttoptr, unknown constants, ...: may be a heap pointer.
    return false;
  }

  return true;
}

// Every caller of F is a direct call in this module, so the arguments F
// receives are exactly the ones its call sites pass.
static bool hasOnlyKnownCallers(const Function &F) {
  if (F.isDeclaration() || !F.hasLocalLinkage())
    return false;
  for (const Use &U : F.uses()) {
    auto *CB = dyn_cast<CallBase>(U.getUser());
    if (!CB || !CB->isCallee(&U) ||
        CB->getFunctionType() != F.getFunctionType())
      return false;
  }
  return true;
}

bool ShadowBoundIPOInfo::isTrustedStructField(Function &F, Value *Src) const {
  if (TrustedStructFields.empty())
    return false;
  auto *LI = dyn_cast<LoadInst>(Src);
  if (!LI)
    return false;
  std::unique_ptr<StructMemberIdent> SMI(
      findStructMember(&F, LI->getPointerOperand()));
  return SMI && TrustedStructFields.count({SMI->getName(), SMI->getIndex()});
}

// The size is computed from exactly one loaded value (e.g. `s->len * 4`).
static bool isSizedByOneLoad(Value *Size) {
  SmallPtrSet<Value *, 16> Visited;
  SmallVector<Value *, 16> Worklist{Size};
  LoadInst *Found = nullptr;
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;
    if (auto *LI = dyn_cast<LoadInst>(V)) {
      if (Found && Found != LI)
        return false;
      Found = LI;
      continue;
    }
    if (auto *I = dyn_cast<Instruction>(V))
      for (Value *Op : I->operands())
        Worklist.push_back(Op);
  }
  return Found != nullptr;
}

// Port of analyzer/src/harness/struct.cpp: a field is trusted if the result of
// malloc / new[] whose size comes from one loaded length is stored into it, on
// every path after the allocation.
static void collectTrustedStructFields(Module &M, ModuleAnalysisManager &AM,
                                       ShadowBoundIPOInfo &Info) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  const DataLayout &DL = M.getDataLayout();

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    DominatorTree *DT = nullptr;
    PostDominatorTree *PDT = nullptr;

    for (Instruction &I : instructions(F)) {
      auto *CB = dyn_cast<CallBase>(&I);
      Function *Callee = CB ? CB->getCalledFunction() : nullptr;
      if (!Callee || CB->arg_size() != 1 ||
          (Callee->getName() != "malloc" && Callee->getName() != "_Znam"))
        continue;
      if (!isSizedByOneLoad(CB->getArgOperand(0)))
        continue;
      if (!DT) {
        DT = &FAM.getResult<DominatorTreeAnalysis>(F);
        PDT = &FAM.getResult<PostDominatorTreeAnalysis>(F);
      }

      // Follow the allocation through casts and constant-offset GEPs to the
      // stores that save it.
      SmallPtrSet<Value *, 16> Visited;
      SmallVector<Value *, 16> Worklist{CB};
      while (!Worklist.empty()) {
        Value *V = Worklist.pop_back_val();
        if (!Visited.insert(V).second)
          continue;
        for (User *U : V->users()) {
          if (isa<BitCastInst>(U)) {
            Worklist.push_back(U);
          } else if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            APInt Off(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
            if (GEP->accumulateConstantOffset(DL, Off))
              Worklist.push_back(GEP);
          } else if (auto *ST = dyn_cast<StoreInst>(U)) {
            if (ST->getValueOperand() != V || !DT->dominates(CB, ST) ||
                !PDT->dominates(ST, CB))
              continue;
            std::unique_ptr<StructMemberIdent> SMI(
                findStructMember(&F, ST->getPointerOperand()));
            if (SMI)
              Info.TrustedStructFields.insert(
                  {SMI->getName(), SMI->getIndex()});
          }
        }
      }
    }
  }
}

ShadowBoundIPOInfo ShadowBoundIPOAnalysis::run(Module &M,
                                               ModuleAnalysisManager &AM) {
  ShadowBoundIPOInfo Info;
  if (ClStructHeuristic)
    collectTrustedStructFields(M, AM, Info);
  if (!ClNonHeapOpt)
    return Info;

  // The functions that directly call each function.
  DenseMap<const Function *, SmallSetVector<Function *, 4>> Callers;
  for (Function &F : M)
    for (Instruction &I : instructions(F))
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (Function *Callee = CB->getCalledFunction())
          Callers[Callee].insert(&F);

  // Optimistic start: assume every candidate fact holds, then remove the ones
  // a call site or return contradicts until nothing changes (greatest
  // fixpoint). Removing a fact can only turn more values into "maybe heap", so
  // this terminates, and what survives is consistent with every call edge.
  for (Function &F : M) {
    if (hasOnlyKnownCallers(F))
      for (Argument &A : F.args())
        if (A.getType()->isPointerTy())
          Info.NonHeapArgs.insert(&A);
    if (!F.isDeclaration() && F.hasExactDefinition() &&
        F.getReturnType()->isPointerTy())
      Info.NonHeapReturns.insert(&F);
  }

  // Facts about a value in function D depend on D's argument facts and on the
  // return facts of D's callees. So when a fact of F is removed, the functions
  // to re-examine are F itself (argument removed) or F's callers (return
  // removed).
  SetVector<Function *> Dirty;
  for (Function &F : M)
    if (!F.isDeclaration())
      Dirty.insert(&F);

  while (!Dirty.empty()) {
    Function *D = Dirty.pop_back_val();

    // Re-check D's return.
    if (Info.NonHeapReturns.count(D)) {
      for (BasicBlock &BB : *D)
        if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator()))
          if (!Info.isNonHeap(RI->getReturnValue())) {
            Info.NonHeapReturns.erase(D);
            for (Function *Caller : Callers.lookup(D))
              Dirty.insert(Caller);
            break;
          }
    }

    // Re-check the arguments D passes to its callees.
    for (Instruction &I : instructions(*D)) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      Function *Callee = CB->getCalledFunction();
      if (!Callee || Callee->isDeclaration())
        continue;
      for (Argument &A : Callee->args()) {
        if (!Info.NonHeapArgs.count(&A))
          continue;
        if (!Info.isNonHeap(CB->getArgOperand(A.getArgNo()))) {
          Info.NonHeapArgs.erase(&A);
          Dirty.insert(Callee);
        }
      }
    }
  }

  NumIPONonHeapArgs += Info.NonHeapArgs.size();
  NumIPONonHeapRets += Info.NonHeapReturns.size();
  LLVM_DEBUG(dbgs() << "[shadowbound-ipo] non-heap args: "
                    << Info.NonHeapArgs.size()
                    << ", non-heap returns: " << Info.NonHeapReturns.size()
                    << "\n");
  return Info;
}
