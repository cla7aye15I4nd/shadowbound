#ifndef LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H
#define LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"

#include <string>

namespace llvm {

class Semantic {
  StringRef Name;
  int PointerField;
  int LengthField;
  int Slope;
  int Intercept;

public:
  Semantic(StringRef Name, int PointerField, int LengthField,
           int Slope, int Intercept)
      : Name(Name), PointerField(PointerField), LengthField(LengthField),
        Slope(Slope), Intercept(Intercept) {}
  
  StringRef getName() const { return Name; }
  int getPointerField() const { return PointerField; }
  int getLengthField() const { return LengthField; }
  int getSlope() const { return Slope; }
  int getIntercept() const { return Intercept; }
};

ArrayRef<Semantic> parseSemaFile(std::string Filename);
bool getPtrDesc(Value *V, StringRef &Name, int &Field);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H