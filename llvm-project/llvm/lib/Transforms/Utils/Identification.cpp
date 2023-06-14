#include "llvm/Transforms/Utils/Identification.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace llvm;
using namespace std;

namespace llvm {

vector<ArrayPatternBase *> parseAPFile(string Filename) {
  vector<ArrayPatternBase *> APats;

  ifstream fin(Filename);
  if (!fin.is_open()) {
    errs() << "Error: cannot open file " << Filename << "\n";
    return {};
  }

  string raw((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
  fin.close();

  auto JSON = json::parse(raw);
  if (!JSON) {
    errs() << "Error: cannot parse file " << Filename << "\n";
    return {};
  }
  if (!JSON->getAsArray()) {
    errs() << "Error: top-level value is not a JSON array: " << Filename
           << "\n";
    return {};
  }

  for (json::Value &V : *JSON->getAsArray()) {
    json::Object *O = V.getAsObject();
    if (!O) {
      errs() << "Error: cannot parse object " << Filename << "\n";
      return {};
    }

    Optional<StringRef> Name = O->getString("id");
    Optional<int64_t> PointerField = O->getInteger("ptr");
    Optional<int64_t> LengthField = O->getInteger("len");
    Optional<int64_t> Slope = O->getInteger("k");
    Optional<int64_t> Intercept = O->getInteger("b");

    if (!Name || !PointerField || !LengthField || !Slope || !Intercept) {
      errs() << "Error: cannot parse field " << Filename << "\n";
      return {};
    }

    ArrayPatternBase *AP = nullptr;
    if (Name->startswith("struct."))
      AP = new StructFieldAP(Name->str(), PointerField.getValue(),
                             LengthField.getValue(), Slope.getValue(),
                             Intercept.getValue());
    else
      AP = new FunArgAP(Name->str(), PointerField.getValue(),
                        LengthField.getValue(), Slope.getValue(),
                        Intercept.getValue());

    APats.push_back(AP);
  }

  return APats;
}

ArrayPatternBase *getArrayPattern(Value *Src, Instruction *I) {
  if (auto *LI = dyn_cast<LoadInst>(Src)) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(LI->getPointerOperand())) {
      // TODO: more precise pattern matching
      if (auto *STy = dyn_cast<StructType>(
              GEP->getPointerOperand()->getType()->getPointerElementType())) {
        if (GEP->getNumIndices() == 2) {
          if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
            StructFieldAP *AP = new StructFieldAP(
                STy->getName().str(), CI->getZExtValue(), 0);

            return AP;
          }
        }
      }
    }
  }

  return nullptr;
}

} // end namespace llvm