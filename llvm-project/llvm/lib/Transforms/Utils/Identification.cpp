#include "llvm/Transforms/Utils/Identification.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace llvm;
using namespace std;

namespace llvm {

vector<Semantic*> parseSemaFile(string Filename) {
  vector<Semantic*> Semantics;

  ifstream fin(Filename);
  if (!fin.is_open()) {
    errs() << "Error: cannot open file " << Filename << "\n";
    return vector<Semantic*>();
  }

  string raw((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
  fin.close();

  auto JSON = json::parse(raw);
  if (!JSON) {
    errs() << "Error: cannot parse file " << Filename << "\n";
    return vector<Semantic*>();
  }
  if (!JSON->getAsArray()) {
    errs() << "Error: top-level value is not a JSON array: " << Filename
           << "\n";
    return vector<Semantic*>();
  }

  for (json::Value &V : *JSON->getAsArray()) {
    json::Object *O = V.getAsObject();
    if (!O) {
      errs() << "Error: cannot parse object " << Filename << "\n";
      return vector<Semantic*>();
    }

    Optional<StringRef> Name = O->getString("id");
    Optional<int64_t> PointerField = O->getInteger("ptr");
    Optional<int64_t> LengthField = O->getInteger("len");
    Optional<int64_t> Slope = O->getInteger("k");
    Optional<int64_t> Intercept = O->getInteger("b");

    if (!Name || !PointerField || !LengthField || !Slope || !Intercept) {
      errs() << "Error: cannot parse field " << Filename << "\n";
      return vector<Semantic*>();
    }

    Semantic *S = new Semantic(Name.getValue().str(), PointerField.getValue(),
                              LengthField.getValue(), Slope.getValue(),
                              Intercept.getValue());

    Semantics.push_back(S);
  }

  return Semantics;
}

bool getPtrDesc(Value *V, string &Name, int &Field) {
  if (auto *LI = dyn_cast<LoadInst>(V)) {
    if (auto *GM = dyn_cast<GetElementPtrInst>(LI->getPointerOperand())) {
      if (auto *STy = dyn_cast<StructType>(
              GM->getPointerOperand()->getType()->getPointerElementType())) {
        if (GM->getNumIndices() == 2) {
          if (auto *CI = dyn_cast<ConstantInt>(GM->getOperand(2))) {
            Name = STy->getName().str();
            Field = CI->getZExtValue();
            return true;
          }
        }
      }
    }
  }
  return false;
}

} // end namespace llvm