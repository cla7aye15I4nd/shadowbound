#ifndef LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H
#define LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"

#include <string>
#include <vector>

#define ASSERT(X)                                                              \
  do {                                                                         \
    if (!(X)) {                                                                \
      printf("Assertion failed at Identification.h: " #X "\n");                \
      abort();                                                                 \
    }                                                                          \
  } while (0)

namespace llvm {

enum APType {
  APT_FUNARG,
  APT_STRUCT,
};

class ArrayPatternBase {
  APType Type;
  unsigned int Slope;
  unsigned int Intercept;

public:
  ArrayPatternBase(APType Type, unsigned int Slope, unsigned int Intercept)
      : Type(Type), Slope(Slope), Intercept(Intercept) {}
  APType getType() const { return Type; }
  unsigned int getSlope() const { return Slope; }
  unsigned int getIntercept() const { return Intercept; }

  virtual void print(raw_ostream &OS) const { ASSERT(0); }
  virtual bool matchPointer(ArrayPatternBase *AP) { return false; }

  friend raw_ostream &operator<<(raw_ostream &OS, const ArrayPatternBase &AP) {
    AP.print(OS);
    return OS;
  }
};

class FunArgAP : public ArrayPatternBase {
  std::string FnName;
  unsigned int PointerArg;
  unsigned int LengthArg;

public:
  FunArgAP(std::string FnName, unsigned int PointerArg, unsigned int LengthArg,
           unsigned int Slope = -1, unsigned int Intercept = -1)
      : ArrayPatternBase(APT_FUNARG, Slope, Intercept), FnName(FnName),
        PointerArg(PointerArg), LengthArg(LengthArg) {}

  void print(raw_ostream &OS) const override {
    OS << "FunArgAP(" << FnName << ", " << PointerArg << ", " << LengthArg
       << ", " << getSlope() << ", " << getIntercept() << ")";
  }

  bool matchPointer(ArrayPatternBase *AP) override {
    if (AP == nullptr)
      return false;
    if (AP->getType() != APT_FUNARG)
      return false;
    FunArgAP *FAP = (FunArgAP *)AP;
    return FnName == FAP->FnName && PointerArg == FAP->PointerArg;
  }
};

class StructFieldAP : public ArrayPatternBase {
  std::string StructName;
  unsigned int PointerField;
  unsigned int LengthField;

public:
  StructFieldAP(std::string StructName, unsigned int PointerField,
                unsigned int LengthField, unsigned int Slope = -1,
                unsigned int Intercept = -1)
      : ArrayPatternBase(APT_STRUCT, Slope, Intercept), StructName(StructName),
        PointerField(PointerField), LengthField(LengthField) {}

  void print(raw_ostream &OS) const override {
    OS << "StructFieldAP(" << StructName << ", " << PointerField << ", "
       << LengthField << ", " << getSlope() << ", " << getIntercept() << ")";
  }

  bool matchPointer(ArrayPatternBase *AP) override {
    if (AP == nullptr)
      return false;
    if (AP->getType() != APT_STRUCT)
      return false;
    StructFieldAP *SAP = (StructFieldAP *)AP;
    return StructName == SAP->StructName && PointerField == SAP->PointerField;
  }
};

std::vector<ArrayPatternBase *> parseAPFile(std::string Filename);
ArrayPatternBase *getArrayPattern(Value *Src, Instruction *I);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H