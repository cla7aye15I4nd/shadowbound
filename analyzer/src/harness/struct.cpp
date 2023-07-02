#include "pattern.h"
#include "llvm/ADT/DenseMap.h"
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

static vector<pair<StoreInst *, int64_t>> findStorePlace(Value *V, DataLayout *DL) {
  vector<pair<StoreInst *, int64_t>> result;

  SmallVector<Value *, 16> Worklist;
  SmallPtrSet<Value *, 16> Visited;
  SmallPtrSet<StoreInst *, 16> Stores;
  DenseMap<Value *, int64_t> Offset;

  Worklist.push_back(V);
  Offset[V] = 0;

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
          if (Stores.find(ST) == Stores.end()) {
            Stores.insert(ST);
            result.push_back(make_pair(ST, Offset[V]));
          }          
        }
      }
    }
  }

  return result;
}

static void findSafeStructMembersInCall(CallBase *CI, Function *F,
                                        vector<Module *> &Modules,
                                        vector<PatternBase *> &Patterns) {
  Function *CalledFn = CI->getCalledFunction();
  if (!CalledFn)
    return;

  if (CalledFn->getName() != "_Znam")
    return;

  DataLayout *DL = getDataLayout(F->getParent());
  vector<pair<StoreInst *, int64_t>> StorePlaces = findStorePlace(CI, DL);
}

void findSafeStructMembers(vector<Module *> Modules,
                           vector<PatternBase *> &Patterns) {
  for (auto *M : Modules)
    for (auto &F : *M)
      for (auto &BB : F)
        for (auto &I : BB)
          if (auto *CI = dyn_cast<CallBase>(&I))
            findSafeStructMembersInCall(CI, &F, Modules, Patterns);
}
