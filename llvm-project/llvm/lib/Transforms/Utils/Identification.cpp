#include "llvm/Transforms/Utils/Identification.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace llvm;
using namespace std;

namespace llvm {

StructMemberIdent *findStructMember(Function *F, Value *V) {
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

    if (LastTy == nullptr)
      return nullptr;
    if (auto *STy = dyn_cast<StructType>(LastTy)) {
      assert(index != -1);
      if (STy->hasName())
        return new StructMemberIdent(STy->getName().str(), index);
    }

    return nullptr;
  }

  return nullptr;
}


vector<PatternBase *> parsePatternFile(string Filename) {
#define ERROR_HANDLER(X)                                                       \
  do {                                                                         \
    if (!(X)) {                                                                \
      errs() << "Error: Wrong File Format (" << Filename << ")\n";             \
      return {};                                                               \
    }                                                                          \
  } while (0)

  vector<PatternBase *> Patterns;

  ifstream fin(Filename);
  ERROR_HANDLER(fin.is_open());

  string raw((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
  fin.close();

  auto JSON = json::parse(raw);
  ERROR_HANDLER(!!JSON && JSON->getAsArray() != nullptr);

  for (json::Value &V : *JSON->getAsArray()) {
    json::Object *O = V.getAsObject();
    ERROR_HANDLER(O != nullptr && O->get("type") != nullptr &&
                  O->get("type")->getAsString().hasValue() &&
                  O->get("pattern") != nullptr &&
                  O->get("pattern")->getAsObject() != nullptr);

    StringRef TypeStr = O->get("type")->getAsString().getValue();
    json::Object *Pattern = O->get("pattern")->getAsObject();

    if (TypeStr == "value") {
      ERROR_HANDLER(Pattern->get("type") != nullptr &&
                    Pattern->get("type")->getAsString().hasValue());
      StringRef PatternType = Pattern->get("type")->getAsString().getValue();
      if (PatternType == "funarg") {
        ERROR_HANDLER(Pattern->get("name") != nullptr &&
                      Pattern->get("name")->getAsString().hasValue());
        ERROR_HANDLER(Pattern->get("index") != nullptr &&
                      Pattern->get("index")->getAsNumber().hasValue());
        StringRef Name = Pattern->get("name")->getAsString().getValue();
        unsigned int Index = Pattern->get("index")->getAsNumber().getValue();

        if (Pattern->get("module") == nullptr)
          Patterns.push_back(
              new ValuePattern(new FunArgIdent(Name.str(), Index)));
        else {
          ERROR_HANDLER(Pattern->get("module")->getAsString().hasValue());
          StringRef Module = Pattern->get("module")->getAsString().getValue();
          Patterns.push_back(new ValuePattern(
              new FunArgIdent(Name.str(), Index, Module.str())));
        }
      } else if (PatternType == "struct") {
        ERROR_HANDLER(Pattern->get("name") != nullptr &&
                      Pattern->get("name")->getAsString().hasValue());
        ERROR_HANDLER(Pattern->get("index") != nullptr &&
                      Pattern->get("index")->getAsNumber().hasValue());
        StringRef Name = Pattern->get("name")->getAsString().getValue();
        unsigned int Index = Pattern->get("index")->getAsNumber().getValue();

        Patterns.push_back(
            new ValuePattern(new StructMemberIdent(Name.str(), Index)));
      } else if (PatternType == "global") {
        // TODO: Implement
      }
    } else if (TypeStr == "array") {
      // TODO: Implement
    }
  }

  return Patterns;

#undef ERROR_HANDLER
}

} // end namespace llvm