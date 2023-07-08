#include "pattern.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace std;
using namespace llvm;

static set<pair<string, int>> SafeStructMembers;

static vector<pair<StoreInst *, int64_t>>
findPointerStorePlace(Function *F, Instruction *I, DataLayout *DL) {
  vector<pair<StoreInst *, int64_t>> result;

  SmallVector<Value *, 16> Worklist;
  SmallPtrSet<Value *, 16> Visited;
  SmallPtrSet<StoreInst *, 16> Stores;
  DenseMap<Value *, int64_t> Offset;

  DominatorTree *DT = getDominatorTree(F);
  PostDominatorTree *PDT = getPostDominatorTree(F);

  Worklist.push_back(I);
  Offset[I] = 0;

  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();

    if (Visited.find(V) != Visited.end())
      continue;

    for (auto *U : V->users()) {
      if (auto *BC = dyn_cast<BitCastInst>(U)) {
        Worklist.push_back(BC);
        Offset[BC] = Offset[V];
      } else if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        APInt OffsetInt(64, 0);
        if (GEP->accumulateConstantOffset(*DL, OffsetInt)) {
          Worklist.push_back(GEP);
          Offset[GEP] = Offset[V] + OffsetInt.getSExtValue();
        }
      } else if (auto *ST = dyn_cast<StoreInst>(U)) {
        if (ST->getValueOperand() == V) {
          if (Stores.find(ST) == Stores.end() && DT->dominates(I, ST) &&
              PDT->dominates(ST, I)) {
            Stores.insert(ST);
            result.push_back(make_pair(ST, Offset[V]));
          }
        }
      }
    }
  }

  return result;
}

static LoadInst *findLengthLoadPlace(Function *F, Value *Val) {
  LoadInst *result = nullptr;

  SmallVector<Value *, 16> Worklist;
  SmallPtrSet<Value *, 16> Visited;

  Worklist.push_back(Val);

  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();

    if (Visited.find(V) != Visited.end())
      continue;

    Visited.insert(V);

    if (auto *I = dyn_cast<Instruction>(V)) {
      for (auto &U : I->operands()) {
        if (auto *LI = dyn_cast<LoadInst>(U)) {
          if (result == nullptr)
            result = LI;
          else
            return nullptr;
        } else {
          Worklist.push_back(U);
        }
      }
    }
  }

  return result;
}

static StructMemberIdent *findStructMember(Function *F, Value *V) {
  Type *Ty = V->getType();
  if (auto *STy = dyn_cast<StructType>(Ty)) {
    if (STy->hasName()) {
      StructMemberIdent *SMI = new StructMemberIdent(STy->getName().str(), 0);
      return SMI;
    }
  }

  if (auto *BC = dyn_cast<BitCastInst>(V))
    return findStructMember(F, BC->getOperand(0));

  if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
    bool isFirstField = true;
    int index = -1;
    Type *LastTy = nullptr;
    Type *Ty = GEP->getPointerOperandType()->getPointerElementType();

    if (!Ty->isStructTy())
      return nullptr;

    for (auto &Op : GEP->indices()) {
      if (isFirstField) {
        isFirstField = false;
        continue;
      }

      auto value = Op.get();
      if (value->getType()->isIntegerTy(32)) {
        StructType *STy = cast<StructType>(Ty);
        LastTy = Ty;
        index = cast<ConstantInt>(value)->getZExtValue();
        Ty = STy->getElementType(index);
      } else {
        auto Aty = cast<ArrayType>(Ty);
        LastTy = Ty;
        index = -1;
        Ty = Aty->getArrayElementType();
      }
    }

    if (auto *STy = dyn_cast<StructType>(LastTy)) {
      assert(index != -1);
      if (STy->hasName())
        return new StructMemberIdent(STy->getName().str(), index);
    }

    return nullptr;
  }

  return nullptr;
}

static void findSafeStructMembersInCall(Function *F, CallBase *CI,
                                        vector<Module *> &Modules,
                                        vector<PatternBase *> &Patterns) {
  Function *CalledFn = CI->getCalledFunction();
  if (!CalledFn)
    return;

  if (CalledFn->getName() != "_Znam" && CalledFn->getName() != "malloc")
    return;

  assert(CI->arg_size() == 1);

  DataLayout *DL = getDataLayout(F->getParent());
  for (auto &SP : findPointerStorePlace(F, CI, DL)) {
    StoreInst *ST = SP.first;
    int64_t Offset = SP.second;

    StructMemberIdent *PtrSM = findStructMember(F, ST->getPointerOperand());
    if (PtrSM) {
      if (SafeStructMembers.find(make_pair(
              PtrSM->getName(), PtrSM->getIndex())) != SafeStructMembers.end())
        continue;

      Value *Size = CI->getArgOperand(0);
      LoadInst *LI = findLengthLoadPlace(F, Size);
      if (LI)
        Patterns.push_back(new ValuePattern(PtrSM));
      else {
        // Store Pattern (not implemented yet)
      }
    }
  }
}

void findSafeStructMembers(vector<Module *> Modules,
                           vector<PatternBase *> &Patterns) {
  for (auto *M : Modules)
    for (auto &F : *M)
      for (auto &BB : F)
        for (auto &I : BB)
          if (auto *CI = dyn_cast<CallBase>(&I))
            findSafeStructMembersInCall(&F, CI, Modules, Patterns);
}
