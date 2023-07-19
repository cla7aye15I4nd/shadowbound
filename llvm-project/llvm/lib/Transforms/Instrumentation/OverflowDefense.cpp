//===- OverflowDefense.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/OverflowDefense.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Identification.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>

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

static const int kReservedBytes = 0x20;

static const uint64_t kShadowBase = ~0x7ULL;
static const uint64_t kShadowMask = ~0x400000000007ULL;
static const uint64_t kHeapSpaceBeg = 0x600000000000ULL;
static const uint64_t kHeapSpaceEnd = 0x700000000000ULL;
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

// ==== Check Type Option ==== //
static cl::opt<bool> ClCheckHeap("odef-check-heap",
                                 cl::desc("check heap memory"), cl::Hidden,
                                 cl::init(true));

static cl::opt<bool> ClCheckStack("odef-check-stack",
                                  cl::desc("check stack memory"), cl::Hidden,
                                  cl::init(false));

static cl::opt<bool> ClCheckGlobal("odef-check-global",
                                   cl::desc("check global memory"), cl::Hidden,
                                   cl::init(false));

static cl::opt<bool> ClCheckInField("odef-check-in-field",
                                    cl::desc("check in-field memory"),
                                    cl::Hidden, cl::init(false));

// ==== Optimization Option ==== //
static cl::opt<bool>
    ClStructPointerOpt("odef-struct-pointer-opt",
                       cl::desc("optimize struct pointer checks"), cl::Hidden,
                       cl::init(true));

static cl::opt<bool> ClOnlySmallAllocOpt("odef-only-small-alloc-opt",
                                         cl::desc("optimize only small alloc"),
                                         cl::Hidden, cl::init(true));

static cl::opt<bool> ClDependenceOpt("odef-dependence-opt",
                                     cl::desc("optimize dependence checks"),
                                     cl::Hidden, cl::init(true));

static cl::opt<bool> ClLoopOpt("odef-loop-opt",
                               cl::desc("optimize loop checks"), cl::Hidden,
                               cl::init(false));

static cl::opt<bool> ClTailCheck("odef-tail-check",
                                 cl::desc("check tail of array"), cl::Hidden,
                                 cl::init(false));

static cl::opt<std::string> ClPatternOptFile("odef-pattern-opt-file",
                                             cl::desc("pattern opt file"),
                                             cl::Hidden, cl::init(""));

// ==== Debug Option ==== //
static cl::opt<std::string> ClWhiteList("odef-whitelist",
                                        cl::desc("whitelist file"), cl::Hidden,
                                        cl::init(""));

static cl::opt<bool> ClDumpIR("odef-dump-ir", cl::desc("dump IR"), cl::Hidden,
                              cl::init(false));

const char kOdefModuleCtorName[] = "odef.module_ctor";
const char kOdefInitName[] = "__odef_init";
const char kOdefReportName[] = "__odef_report";
const char kOdefAbortName[] = "__odef_abort";
const char kOdefSetShadowName[] = "__odef_set_shadow";

namespace {

enum CheckType {
  kRuntimeCheck = 0,
  kClusterCheck = 1,
  kBuiltInCheck = 2,
  kInFieldCheck = 3,
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

struct FieldCheck : public BaseCheck {
  GetElementPtrInst *Gep;
  SmallVector<std::pair<Value *, uint64_t>, 16> SubFields;

  FieldCheck(GetElementPtrInst *Gep,
             SmallVector<std::pair<Value *, uint64_t>, 16> SubFields)
      : BaseCheck(kInFieldCheck), Gep(Gep), SubFields(SubFields) {}
};

struct ClusterCheck : public BaseCheck {
  Value *Src;
  SmallVector<Instruction *, 16> Insts;

  ClusterCheck(Value *Src, SmallVector<Instruction *, 16> Insts)
      : BaseCheck(kClusterCheck), Src(Src), Insts(Insts) {}
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
  bool patternMatch(Function &F, Instruction *I, PatternBase *P);
  void patternOptimize(Function &F);
  void dependencyOptimize(Function &F, DominatorTree &DT,
                          PostDominatorTree &PDT, ScalarEvolution &SE);
  void loopOptimize(Function &F, LoopInfo &LI, ScalarEvolution &SE,
                    DominatorTree &DT, PostDominatorTree &PDT);
  void collectMonoLoop(Function &F, LoopInfo &LI, ScalarEvolution &SE);
  bool monotonicLoopOptimize(Function &F, Value *Addr, Loop *L,
                             ScalarEvolution &SE);

  SmallVector<BitCastInst *, 16> dependencyOptimizeForBc(Function &F,
                                                         DominatorTree &DT,
                                                         PostDominatorTree &PDT,
                                                         ScalarEvolution &SE);

  SmallVector<GetElementPtrInst *, 16>
  dependencyOptimizeForGep(Function &F, DominatorTree &DT,
                           PostDominatorTree &PDT, ScalarEvolution &SE);
  void collectSubFieldCheck(Function &F, ScalarEvolution &SE);
  void collectChunkCheck(Function &F, LoopInfo &LI,
                         ObjectSizeOffsetEvaluator &ObjSizeEval,
                         ScalarEvolution &SE);
  void collectChunkCheckImpl(Function &F, Value *Src,
                             SmallVector<Instruction *, 16> &Insts,
                             LoopInfo &LI,
                             ObjectSizeOffsetEvaluator &ObjSizeEval,
                             ScalarEvolution &SE);
  bool tryRuntimeFreeCheck(Function &F, Value *Src,
                           SmallVector<Instruction *, 16> &Insts,
                           ObjectSizeOffsetEvaluator &ObjSizeEval);

  void commitInstrument(Function &F);
  void commitFieldCheck(Function &F, FieldCheck &Check);
  void commitBuiltInCheck(Function &F, BuiltinCheck &Check);
  void commitClusterCheck(Function &F, ClusterCheck &Check);
  void commitRuntimeCheck(Function &F, RuntimeCheck &Check);

  void instrumentBitCast(Function &F, Value *Src, BitCastInst *BC);
  void instrumentGep(Function &F, Value *Src, GetElementPtrInst *GEP);

  Value *getSource(Value *I);
  Value *getSourceImpl(Value *V);
  OffsetDir getOffsetDir(Value *Addr);
  void setOffsetDir(Value *Addr, ScalarEvolution &SE);
  bool getPhiSource(Value *V, Value *&Src, SmallPtrSet<Value *, 16> &Visited);

  void CreateTrapBB(BuilderTy &B, Value *Cond, bool Abort);
  Value *readRegister(Function &F, BuilderTy &IRB, StringRef RegName);

  StructType *sourceAnalysis(Function &F, Value *Src);

  bool Kernel;
  bool Recover;

  SmallVector<GetElementPtrInst *, 16> GepToInstrument;
  SmallVector<BitCastInst *, 16> BcToInstrument;

  SmallVector<GetElementPtrInst *, 16> SubFieldToInstrument;

  DenseMap<Value *, OffsetDir> OffsetDirCache;
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
};

bool isEscapeInstruction(Instruction *I, Value *V) {
  // TODO: Add uncommon escape instructions
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

bool isStdFunction(StringRef name) {
  std::string cmd = "c++filt " + name.str();
  FILE *pipe = popen(cmd.c_str(), "r");

  std::string result;
  char buffer[0x100];
  while (fgets(buffer, sizeof buffer, pipe) != NULL)
    result += buffer;

  pclose(pipe);

  std::string fname;
  size_t start_pos = 0, end_pos = 0, count = 0;

  while (end_pos < result.size()) {
    if (result[end_pos] == '(') {
      fname = result.substr(start_pos, end_pos - start_pos);
      break;
    }

    if (result[end_pos] == '<' && result[end_pos + 1] != '(')
      count++;
    else if (result[end_pos] == '>')
      count--;
    else if (result[end_pos] == ' ' && count == 0)
      start_pos = end_pos + 1;

    end_pos++;
  }

  return StringRef(fname).startswith("std::");
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
  M.getOrInsertFunction(kOdefSetShadowName, Type::getVoidTy(C),
                        Type::getInt64Ty(C), Type::getInt64Ty(C),
                        Type::getInt64Ty(C));
}

void insertGlobalVariable(Module &M) {
  LLVMContext &C = M.getContext();
  M.getOrInsertGlobal("__odef_only_small_alloc_opt", Type::getInt32Ty(C), [&] {
    return new GlobalVariable(
        M, Type::getInt32Ty(C), true, GlobalValue::WeakODRLinkage,
        ConstantInt::get(Type::getInt32Ty(C), ClOnlySmallAllocOpt ? 1 : 0),
        "__odef_only_small_alloc_opt");
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
  if (ClDumpIR) {
    std::error_code EC;
    raw_fd_ostream OS(M.getSourceFileName() + ".bc", EC, sys::fs::OF_None);
    WriteBitcodeToFile(M, OS);
  }

  if (Options.Kernel)
    return PreservedAnalyses::all();
  insertModuleCtor(M);
  insertRuntimeFunction(M);
  insertGlobalVariable(M);
  return PreservedAnalyses::none();
}

void OverflowDefense::initializeModule(Module &M) {
  LLVMContext &C = M.getContext();

  DL = &M.getDataLayout();

  M.getOrInsertFunction(kOdefReportName, Type::getVoidTy(C));
  M.getOrInsertFunction(kOdefAbortName, Type::getVoidTy(C));

  ReportFn = M.getFunction(kOdefReportName);
  AbortFn = M.getFunction(kOdefAbortName);
  SetShadowFn = M.getFunction(kOdefSetShadowName);

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

  if (isStdFunction(F.getName()))
    return false;

  if (WhiteList.find(F.getName()) != WhiteList.end())
    return false;

  dbgs() << "[" << F.getName() << "]\n";

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

  dependencyOptimize(F, DT, PDT, SE);
  loopOptimize(F, LI, SE, DT, PDT);
  if (ClLoopOpt) {
    // Loop Optimization may introduce new instructions to instrument
    dependencyOptimize(F, DT, PDT, SE);
  }
  structPointerOptimizae(F, SE);
  patternOptimize(F);

  // Instrument subfield access
  // TODO: instrument subfield access do not require *any* runtime support, but
  // we still need to know how much they cost
  collectSubFieldCheck(F, SE);

  // Instrument GEP and BC
  collectChunkCheck(F, LI, ObjSizeEval, SE);

  commitInstrument(F);

  if (std::accumulate(Counter, Counter + kCheckTypeEnd, 0) > 0) {
    dbgs() << "  Builtin Check: " << Counter[kBuiltInCheck] << "\n";
    dbgs() << "  Cluster Check: " << Counter[kClusterCheck] << "\n";
    dbgs() << "  Runtime Check: " << Counter[kRuntimeCheck] << "\n";
    dbgs() << "  InField Check: " << Counter[kInFieldCheck] << "\n";
  }

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

bool OverflowDefense::filterToInstrument(Function &F, Instruction *I,
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

bool OverflowDefense::isZeroAccessGep(const DataLayout *DL, Instruction *I) {
  auto *Gep = dyn_cast<GetElementPtrInst>(I);

  // It is not a GEP
  if (Gep == nullptr)
    return false;

  APInt Offset(DL->getIndexSizeInBits(Gep->getPointerAddressSpace()), 0, true);
  if (!Gep->accumulateConstantOffset(*DL, Offset))
    return false;

  return Offset.ule(GetPtrUsage(I) == kPtrDeref ? kReservedBytes : 0);
}

PtrUsage OverflowDefense::GetPtrUsage(Instruction *I) {
  SmallVector<Instruction *, 16> WorkList;
  SmallPtrSet<Instruction *, 16> Visited;

  if (PtrUsageCache.count(I))
    return PtrUsageCache[I];

  WorkList.push_back(I);
  while (!WorkList.empty()) {
    Instruction *V = WorkList.pop_back_val();

    if (Visited.count(V))
      continue;

    Visited.insert(V);

    for (auto *U : V->users()) {
      if (auto *UI = dyn_cast<Instruction>(U)) {
        if (isEscapeInstruction(UI, V))
          return PtrUsageCache[I] = kPtrEscape;
        if (isDerefInstruction(UI, V))
          return PtrUsageCache[I] = kPtrDeref;
        if (isa<PHINode>(UI))
          WorkList.push_back(UI);
      }
    }
  }

  return PtrUsageCache[I] = kPtrNone;
}

bool OverflowDefense::isShrinkBitCast(Instruction *I) {
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

void OverflowDefense::collectSubFieldCheck(Function &F, ScalarEvolution &SE) {
  for (auto *Gep : SubFieldToInstrument) {
    Type *Ty = Gep->getPointerOperandType()->getPointerElementType();

    if (isa<GlobalVariable>(Gep->getPointerOperand())) {
      // TODO: how to handle global variable's size?
      continue;
    }

    if (isFixedSizeType(Ty)) {
      bool skipOnce = false;
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
          ASSERT(!skipOnce);

          StructType *STy = cast<StructType>(Ty);
          auto index = cast<ConstantInt>(value)->getZExtValue();
          Ty = STy->getElementType(index);

          if (isFlexibleStructure(STy) && index == STy->getNumElements() - 1)
            skipOnce = true;
        } else {
          ASSERT(value->getType()->isIntegerTy(64));
          ASSERT(Ty->isArrayTy());

          auto Aty = cast<ArrayType>(Ty);

          if (skipOnce) {
            skipOnce = false;
          } else if (SE.getUnsignedRangeMax(SE.getSCEV(value)).getZExtValue() >=
                     Aty->getNumElements()) {
            SubFields.push_back(std::make_pair(value, Aty->getNumElements()));
          }

          Ty = Aty->getArrayElementType();
        }
      }

      if (SubFields.size() > 0) {
        Checks.push_back(new FieldCheck(Gep, SubFields));
      }
    }
  }
}

void OverflowDefense::dependencyOptimize(Function &F, DominatorTree &DT,
                                         PostDominatorTree &PDT,
                                         ScalarEvolution &SE) {

  if (!ClDependenceOpt)
    return;
  SmallVector<BitCastInst *, 16> NewBcToInstrument =
      dependencyOptimizeForBc(F, DT, PDT, SE);
  SmallVector<GetElementPtrInst *, 16> NewGepToInstrument =
      dependencyOptimizeForGep(F, DT, PDT, SE);

  // TODO: optimize for subfield access
  BcToInstrument.swap(NewBcToInstrument);
  GepToInstrument.swap(NewGepToInstrument);
}

bool OverflowDefense::patternMatch(Function &F, Instruction *I,
                                   PatternBase *P) {

  if (P->getType() == PT_VALUE) {
    ValueIdentBase *VI = static_cast<ValuePattern *>(P)->getIdent();

    if (VI->getType() == VIT_FUNARG) {
      FunArgIdent *FAI = static_cast<FunArgIdent *>(VI);

      // F is a static function, we need to check the module name
      if (F.hasLocalLinkage() &&
          !StringRef(F.getParent()->getModuleIdentifier())
               .endswith(FAI->getModuleName()))
        return false;
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
  } else if (P->getType() == PT_ARRAY) {
    // TODO: support array pattern
  }

  return false;
}

void OverflowDefense::patternOptimize(Function &F) {
  if (ClPatternOptFile == "")
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

void OverflowDefense::structPointerOptimizae(Function &F, ScalarEvolution &SE) {
  if (!ClStructPointerOpt)
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
  DenseSet<int> OptimizedIndex;

  for (size_t i = 0; i < GepToInstrument.size(); ++i) {
    bool optimized = false;
    auto I = GepToInstrument[i];

    for (size_t j = 0; j < GepToInstrument.size(); ++j) {
      if (i != j && OptimizedIndex.count(j) == 0) {
        auto J = GepToInstrument[j];
        if (DT.dominates(J, I) || PDT.dominates(J, I)) {

          // The pointer operand of I and J are the same
          if (I->getPointerOperand() == J->getPointerOperand()) {
            APInt IOffset(DL->getIndexSizeInBits(I->getPointerAddressSpace()),
                          0, true);
            APInt JOffset(DL->getIndexSizeInBits(J->getPointerAddressSpace()),
                          0, true);
            if (I->accumulateConstantOffset(*DL, IOffset) &&
                J->accumulateConstantOffset(*DL, JOffset) &&
                ((JOffset.sge(IOffset) && IOffset.sge(0)) ||
                 (JOffset.sle(IOffset) && IOffset.sle(0)))) {
              optimized = true;
              break;
            } else {
              Type *ty =
                  I->getPointerOperand()->getType()->getPointerElementType();

              size_t numIndex =
                  isFixedSizeType(ty)
                      ? 1
                      : std::max(I->getNumIndices(), J->getNumIndices());
              bool Greater = true;

              // Compare the offset of each index if every offset of I is always
              // smaller than J, then I is not need to be instrumented
              for (size_t k = 0; k < numIndex; ++k) {
                auto IntTy = k >= I->getNumIndices()
                                 ? J->getOperand(k + 1)->getType()
                                 : I->getOperand(k + 1)->getType();
                auto IOffset = k >= I->getNumIndices()
                                   ? ConstantInt::getNullValue(IntTy)
                                   : I->getOperand(k + 1);
                auto JOffset = k >= J->getNumIndices()
                                   ? ConstantInt::getNullValue(IntTy)
                                   : J->getOperand(k + 1);

                if (IOffset->getType() != JOffset->getType()) {
                  Greater = false;
                  break;
                }

                // If the max offset of I is larger than the min offset of J,
                // then it is possible that the offset of I is greater than the
                // offset of J at runtime.
                if (IOffset != JOffset &&
                    SE.getUnsignedRangeMin(SE.getSCEV(JOffset))
                        .ult(SE.getUnsignedRangeMax(SE.getSCEV(IOffset)))) {
                  Greater = false;
                  break;
                }
              }
              if (Greater) {
                optimized = true;
                break;
              }
            }
          }

          if (J->getPointerOperand() == I) {
            // TODO: Determine the direction of I is positive or negative
            bool Greater = true;
            for (size_t k = 0; k < J->getNumIndices(); ++k) {
              auto JOffset = J->getOperand(k + 1);
              if (SE.getSignedRangeMin(SE.getSCEV(JOffset)).isNegative()) {
                Greater = false;
                break;
              }
            }
            if (Greater) {
              optimized = true;
              break;
            }
          }
        }
      }
    }

    if (!optimized)
      NewGepToInstrument.push_back(GepToInstrument[i]);
    else
      OptimizedIndex.insert(i);
  }

  return NewGepToInstrument;
}

Value *OverflowDefense::getSource(Value *I) {
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
  // TODO: Maybe we need to handle the Constant NULL Pointer.

  if (Src == nullptr) {
    Src = V;
    return true;
  }
  return Src == V;
}

void OverflowDefense::collectChunkCheck(Function &F, LoopInfo &LI,
                                        ObjectSizeOffsetEvaluator &ObjSizeEval,
                                        ScalarEvolution &SE) {
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
    collectChunkCheckImpl(F, Src, Insts, LI, ObjSizeEval, SE);
  }
}

StructType *OverflowDefense::sourceAnalysis(Function &F, Value *Src) {
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

void OverflowDefense::setOffsetDir(Value *Addr, ScalarEvolution &SE) {
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
      for (auto &Op : GEP->indices()) {
        auto Range = SE.getSCEV(Op.get());
        if (SE.getSignedRangeMin(Range).isNonNegative())
          Dir |= kOffsetPositive;
        else if (SE.getSignedRangeMax(Range).isNegative())
          Dir |= kOffsetNegative;
        else
          Dir |= kOffsetBoth;

        if (Dir == kOffsetBoth)
          break;
      }

      if (Dir == kOffsetBoth)
        break;

      WorkList.push_back(GEP->getPointerOperand());
    }
  }

  OffsetDirCache[Addr] = Dir;
}

OffsetDir OverflowDefense::getOffsetDir(Value *Addr) {
  ASSERT(OffsetDirCache.count(Addr));

  return OffsetDirCache[Addr];
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

bool OverflowDefense::isAccessMemberBoost(Instruction *I, ScalarEvolution &SE) {
  // Optimize the following case:
  //  Obj* obj = ...
  //  u8* buf = (u8*) obj;
  //  buf[1] = 0;

#define HANDLE_GEP(GEP)                                                        \
  do {                                                                         \
    V = GEP->getPointerOperand();                                              \
                                                                               \
    if (GEP->getNumIndices() != 1)                                             \
      return false;                                                            \
                                                                               \
    auto Range = SE.getUnsignedRange(SE.getSCEV(GEP->getOperand(1)));          \
    maxOffset += Range.getUnsignedMax().getZExtValue();                        \
                                                                               \
    if (maxOffset > size)                                                      \
      return false;                                                            \
  } while (0)

  Value *Src = getSource(I);

  ASSERT(Src->getType()->isPointerTy());
  Type *ty = dyn_cast<PointerType>(Src->getType())->getPointerElementType();

  if (isFixedSizeType(ty)) {
    unsigned long long size = getFixedSize(ty, DL);
    unsigned long long maxOffset =
        DL->getTypeStoreSize(I->getType()->getPointerElementType());

    Value *V = I;
    while (V != Src) {
      if (isa<PHINode>(V))
        return false;
      if (auto *BC = dyn_cast<BitCastInst>(V))
        V = BC->getOperand(0);
      else if (auto *BCO = dyn_cast<BitCastOperator>(V))
        V = BCO->getOperand(0);
      else if (auto *GEP = dyn_cast<GetElementPtrInst>(V))
        HANDLE_GEP(GEP);
      else if (auto *GEPO = dyn_cast<GEPOperator>(V))
        HANDLE_GEP(GEPO);
    }

    return true;
  }

  return false;

#undef HANDLE_GEP
}

void OverflowDefense::collectChunkCheckImpl(
    Function &F, Value *Src, SmallVector<Instruction *, 16> &Insts,
    LoopInfo &LI, ObjectSizeOffsetEvaluator &ObjSizeEval, ScalarEvolution &SE) {
  if (tryRuntimeFreeCheck(F, Src, Insts, ObjSizeEval)) {
    return;
  }

  int weight = 0;
  for (auto *I : Insts)
    weight += LI.getLoopFor(I->getParent()) != nullptr ? 5 : 1;

  for (auto *I : Insts)
    setOffsetDir(I, SE);

  StructType *STy = sourceAnalysis(F, Src);
  if (weight <= 2) {
    Checks.push_back(new RuntimeCheck(Src, Insts));
  } else {
    Checks.push_back(new ClusterCheck(Src, Insts));
  }
}

bool OverflowDefense::monotonicLoopOptimize(Function &F, Value *Addr, Loop *Lop,
                                            ScalarEvolution &SE) {
  auto *SCEVPtr = SE.getSCEV(Addr);
  auto *ML = MonoLoopMap[Lop];
  ASSERT(ML != nullptr);

  if (auto *ARE = dyn_cast<SCEVAddRecExpr>(SCEVPtr)) {
    auto *Start = ARE->getStart();
    auto *Step = ARE->getStepRecurrence(SE);

    dbgs() << "[IndGep]\n";
    dbgs() << "Addr: " << *Addr << "\n";
    dbgs() << "Start: " << *Start << "\n";
    dbgs() << "Step: " << *Step << "\n";
  }

  return false;
}

void OverflowDefense::loopOptimize(Function &F, LoopInfo &LI,
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
      MonoLoop *ML = MonoLoopMap[Loop];

      if (ML->getStepInst() == GEP) {
        if (!isa<GetElementPtrInst>(ML->Upper))
          continue;
      }

      if (monotonicLoopOptimize(F, GEP, Loop, SE))
        continue;
    }

    NewGepToInstrument.push_back(GEP);
  }

  GepToInstrument.swap(NewGepToInstrument);
}

void OverflowDefense::collectMonoLoop(Function &F, LoopInfo &LI,
                                      ScalarEvolution &SE) {
  // TODO: This part is very complex and needs to be clarified
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

    dbgs() << "[Mono Loop]\n";
    dbgs() << "IndVar: " << *IndVar << "\n";
    dbgs() << "Lower: " << *Lower << "\n";
    dbgs() << "Upper: " << *Upper << "\n";
    dbgs() << "Step: " << *Step << "\n";
    dbgs() << "StepInst: " << *IndVar->getIncomingValueForBlock(Latch) << "\n";
    dbgs() << "GuardCond: " << *GuardCmp << "\n";
    dbgs() << "ExitCmp: " << *ExitCmp << "\n";

    MonoLoopMap[Loop] =
        new MonoLoop(Loop, IndVar, Lower, Upper, Step, GuardBB, Preheader);
  }
}

bool OverflowDefense::tryRuntimeFreeCheck(
    Function &F, Value *Src, SmallVector<Instruction *, 16> &Insts,
    ObjectSizeOffsetEvaluator &ObjSizeEval) {
  SizeOffsetEvalType SizeOffsetEval = ObjSizeEval.compute(Src);

  if (ObjSizeEval.bothKnown(SizeOffsetEval)) {
    Checks.push_back(new BuiltinCheck(Src, SizeOffsetEval.first,
                                      SizeOffsetEval.second, Insts));
    return true;
  } else if (auto *G = dyn_cast<GlobalVariable>(Src)) {
    // FIXME: handle global variable
    return true;
  }

  return false;
}

void OverflowDefense::instrumentBitCast(Function &F, Value *Src,
                                        BitCastInst *BC) {
  // ShadowAddr = BC & kShadowMask;
  // Base = BC & kShadowBase;
  // BackSize = *(int32_t *) ShadowAddr;
  // if (BC > Base + BackSize - NeededSize)
  //   report_overflow();

  Instruction *InsertPt = BC->hasOneUser() && !isa<PHINode>(BC->user_back())
                              ? BC->user_back()
                              : BC->getInsertionPointAfterDef();

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *CmpPtr = IRB.CreatePtrToInt(BC, int64Type);

  {
    // FIXME: This block can be removed?
    Value *IsApp = IRB.CreateAnd(
        IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kHeapSpaceBeg)),
        IRB.CreateICmpULT(Ptr, ConstantInt::get(int64Type, kHeapSpaceEnd)));
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));
  }

  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));

  Value *BackSizeRaw = IRB.CreateZExt(
      IRB.CreateLoad(int32Type, IRB.CreateIntToPtr(Shadow, int32PtrType)),
      int64Type);
  Value *BackSize =
      ClOnlySmallAllocOpt ? BackSizeRaw : IRB.CreateShl(BackSizeRaw, 3);

  uint64_t NeededSize =
      DL->getTypeStoreSize(BC->getType()->getPointerElementType());
  ASSERT(NeededSize > kReservedBytes);
  Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);

  Value *Cmp = IRB.CreateICmpUGT(
      CmpPtr, IRB.CreateSub(IRB.CreateAdd(Base, BackSize), NeededSizeVal));
  CreateTrapBB(IRB, Cmp, true);
}

void OverflowDefense::instrumentGep(Function &F, Value *Src,
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

  Instruction *InsertPt = GEP->hasOneUser() && !isa<PHINode>(GEP->user_back())
                              ? GEP->user_back()
                              : GEP->getInsertionPointAfterDef();
  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *CmpPtr = IRB.CreatePtrToInt(GEP, int64Type);

  {
    // FIXME: This block can be removed?
    Value *IsApp = IRB.CreateAnd(
        IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kHeapSpaceBeg)),
        IRB.CreateICmpULT(Ptr, ConstantInt::get(int64Type, kHeapSpaceEnd)));
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));
  }

  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));

  Value *Cmp = nullptr;
  if (getOffsetDir(GEP) == kOffsetBoth) {
    Value *Packed =
        IRB.CreateLoad(int64Type, IRB.CreateIntToPtr(Shadow, int64PtrType));
    Value *BackRaw =
        IRB.CreateAnd(Packed, ConstantInt::get(int64Type, 0xffffffff));
    Value *FrontRaw = IRB.CreateLShr(Packed, 32);
    Value *Back = ClOnlySmallAllocOpt ? BackRaw : IRB.CreateShl(BackRaw, 3);
    Value *Front = ClOnlySmallAllocOpt ? FrontRaw : IRB.CreateShl(FrontRaw, 3);
    Value *Begin = IRB.CreateSub(Base, Front);
    Value *End = IRB.CreateAdd(Base, Back);
    Value *CmpBegin = IRB.CreateICmpULT(CmpPtr, Begin);

    uint64_t NeededSize =
        DL->getTypeStoreSize(GEP->getType()->getPointerElementType());
    Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);
    Value *CmpEnd =
        ClTailCheck
            ? IRB.CreateICmpUGT(IRB.CreateAdd(CmpPtr, NeededSizeVal), End)
            : IRB.CreateICmpUGT(CmpPtr, End);
    Cmp = IRB.CreateOr(CmpBegin, CmpEnd);
  } else if (getOffsetDir(GEP) == kOffsetPositive) {
    Value *BackRaw = IRB.CreateZExt(
        IRB.CreateLoad(int32Type, IRB.CreateIntToPtr(Shadow, int32PtrType)),
        int64Type);
    Value *Back = ClOnlySmallAllocOpt ? BackRaw : IRB.CreateShl(BackRaw, 3);

    uint64_t NeededSize =
        DL->getTypeStoreSize(GEP->getType()->getPointerElementType());
    Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);
    Value *End = IRB.CreateAdd(Base, Back);

    Cmp = ClTailCheck
              ? IRB.CreateICmpUGT(IRB.CreateAdd(CmpPtr, NeededSizeVal), End)
              : IRB.CreateICmpUGT(CmpPtr, End);
  } else if (getOffsetDir(GEP) == kOffsetNegative) {
    Value *ShadowP = IRB.CreateAdd(Shadow, ConstantInt::get(int64Type, 4));
    Value *FrontRaw = IRB.CreateZExt(
        IRB.CreateLoad(int32Type, IRB.CreateIntToPtr(ShadowP, int32PtrType)),
        int64Type);
    Value *Front = ClOnlySmallAllocOpt ? FrontRaw : IRB.CreateShl(FrontRaw, 3);

    Cmp = IRB.CreateICmpULT(CmpPtr, IRB.CreateSub(Base, Front));
  }

  ASSERT(Cmp != nullptr);
  CreateTrapBB(IRB, Cmp, true);
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
  for (auto *C_ : Checks) {
    BaseCheck &C = *C_;
    if (C.Type == kBuiltInCheck) {
      commitBuiltInCheck(F, (BuiltinCheck &)C);
    } else if (C.Type == kClusterCheck) {
      commitClusterCheck(F, (ClusterCheck &)C);
    } else if (C.Type == kRuntimeCheck) {
      commitRuntimeCheck(F, (RuntimeCheck &)C);
    } else if (C.Type == kInFieldCheck) {
      commitFieldCheck(F, (FieldCheck &)C);
    } else {
      ASSERT(false);
      __builtin_unreachable();
    }
  }
}

void OverflowDefense::commitFieldCheck(Function &F, FieldCheck &FC) {
  if (!ClCheckInField)
    return;

  ASSERT(FC.Type == kInFieldCheck);
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

void OverflowDefense::commitBuiltInCheck(Function &F, BuiltinCheck &BC) {
  if (!ClCheckStack)
    return;

  ASSERT(BC.Type == kBuiltInCheck);
  Counter[kBuiltInCheck]++;

  Value *Src = BC.Src;
  Instruction *InsertPt =
      isa<Instruction>(Src)
          ? cast<Instruction>(Src)->getInsertionPointAfterDef()
          : &*F.getEntryBlock().getFirstInsertionPt();

  BuilderTy IRB(InsertPt->getParent(), InsertPt->getIterator(),
                TargetFolder(*DL));

  Value *Size = BC.Size;
  Value *Offset = BC.Offset;

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  Value *PtrBegin = IRB.CreateSub(Ptr, Offset);
  Value *PtrEnd = IRB.CreateAdd(Ptr, Size);

  for (auto &I : BC.Insts) {
    IRB.SetInsertPoint(I->getInsertionPointAfterDef());

    uint64_t NeededSize =
        DL->getTypeStoreSize(I->getType()->getPointerElementType());
    Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);

    Value *Addr = IRB.CreatePtrToInt(I, int64Type);
    Value *CmpBegin = IRB.CreateICmpULT(Addr, PtrBegin);
    Value *CmpEnd =
        ClTailCheck
            ? IRB.CreateICmpUGT(Addr, IRB.CreateSub(PtrEnd, NeededSizeVal))
            : IRB.CreateICmpUGT(Addr, PtrEnd);
    Value *Cmp = IRB.CreateOr(CmpBegin, CmpEnd);

    CreateTrapBB(IRB, Cmp, true);
  }
}

void OverflowDefense::commitClusterCheck(Function &F, ClusterCheck &CC) {
  if (!ClCheckHeap)
    return;

  ASSERT(CC.Type == kClusterCheck);
  Counter[kClusterCheck]++;

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
  // Back = Packed & 0xffffffff;
  // Front = Packed >> 32;
  // Begin = Base - (Front << 3);
  // End = Base + (Back << 3);

  Value *Ptr = IRB.CreatePtrToInt(Src, int64Type);
  BasicBlock *Head = IRB.GetInsertBlock();

  {
    // FIXME: This block can be removed?
    Value *IsApp = IRB.CreateAnd(
        IRB.CreateICmpUGE(Ptr, ConstantInt::get(int64Type, kHeapSpaceBeg)),
        IRB.CreateICmpULT(Ptr, ConstantInt::get(int64Type, kHeapSpaceEnd)));
    IRB.SetInsertPoint(SplitBlockAndInsertIfThen(IsApp, InsertPt, false));
  }

  BasicBlock *Then = IRB.GetInsertBlock();

  Value *Base = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowBase));
  Value *Shadow = IRB.CreateAnd(Ptr, ConstantInt::get(int64Type, kShadowMask));
  Value *Packed =
      IRB.CreateLoad(int64Type, IRB.CreateIntToPtr(Shadow, int64PtrType));
  Value *BackRaw =
      IRB.CreateAnd(Packed, ConstantInt::get(int64Type, 0xffffffff));
  Value *FrontRaw = IRB.CreateLShr(Packed, 32);
  Value *Back = ClOnlySmallAllocOpt ? BackRaw : IRB.CreateShl(BackRaw, 3);
  Value *Front = ClOnlySmallAllocOpt ? FrontRaw : IRB.CreateShl(FrontRaw, 3);
  Value *ThenBegin = IRB.CreateSub(Base, Front);
  Value *ThenEnd = IRB.CreateAdd(Base, Back);

  IRB.SetInsertPoint(InsertPt);
  PHINode *Begin = IRB.CreatePHI(int64Type, 2);
  Begin->addIncoming(ThenBegin, Then);
  Begin->addIncoming(ConstantInt::get(int64Type, 0), Head);

  PHINode *End = IRB.CreatePHI(int64Type, 2);
  End->addIncoming(ThenEnd, Then);
  End->addIncoming(ConstantInt::get(int64Type, kMaxAddress), Head);

  // TODO: Move tail check to here
  // Check if Ptr is in [Begin, End).
  for (auto *I : CC.Insts) {
    IRB.SetInsertPoint(I->getInsertionPointAfterDef());
    Value *Ptr = IRB.CreatePtrToInt(I, int64Type);
    uint64_t NeededSize =
        DL->getTypeStoreSize(I->getType()->getPointerElementType());
    Value *NeededSizeVal = ConstantInt::get(int64Type, NeededSize);

    OffsetDir Dir = getOffsetDir(I);

    Value *UpperCmp =
        Dir & kOffsetPositive
            ? IRB.CreateICmpUGT(
                  ClTailCheck ? IRB.CreateAdd(Ptr, NeededSizeVal) : Ptr, End)
            : ConstantInt::getFalse(IRB.getContext());
    Value *LowerCmp = Dir & kOffsetNegative
                          ? IRB.CreateICmpULT(Ptr, Begin)
                          : ConstantInt::getFalse(IRB.getContext());
    Value *NotIn = IRB.CreateOr(UpperCmp, LowerCmp);
    CreateTrapBB(IRB, NotIn, true);
  }
}

void OverflowDefense::commitRuntimeCheck(Function &F, RuntimeCheck &RC) {
  if (!ClCheckHeap)
    return;

  ASSERT(RC.Type == kRuntimeCheck);
  Counter[kRuntimeCheck] += RC.Insts.size();

  Value *Src = RC.Src;

  for (auto *I : RC.Insts) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      instrumentGep(F, Src, GEP);
    } else if (auto *BC = dyn_cast<BitCastInst>(I)) {
      instrumentBitCast(F, Src, BC);
    }
  }
}

Value *OverflowDefense::readRegister(Function &F, BuilderTy &IRB,
                                     StringRef Reg) {
  Module *M = F.getParent();
  Function *readReg = Intrinsic::getDeclaration(M, Intrinsic::read_register,
                                                IRB.getIntPtrTy(*DL));

  LLVMContext &C = M->getContext();
  MDNode *MD = MDNode::get(C, {MDString::get(C, Reg)});
  return IRB.CreateCall(readReg, {MetadataAsValue::get(C, MD)});
}