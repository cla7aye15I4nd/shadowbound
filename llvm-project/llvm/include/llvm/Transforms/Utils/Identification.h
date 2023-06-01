#ifndef LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H
#define LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"

#include <string>
#include <vector>

namespace llvm {

class Semantic {
  std::string Name;
  int PointerField;
  int LengthField;
  int Slope;
  int Intercept;

public:
  Semantic(std::string Name, int PointerField, int LengthField,
           int Slope, int Intercept)
      : Name(Name), PointerField(PointerField), LengthField(LengthField),
        Slope(Slope), Intercept(Intercept) {}
  
  std::string getName() const { return Name; }
  int getPointerField() const { return PointerField; }
  int getLengthField() const { return LengthField; }
  int getSlope() const { return Slope; }
  int getIntercept() const { return Intercept; }
};

std::vector<Semantic*> parseSemaFile(std::string Filename);
bool getPtrDesc(Value *V, std::string &Name, int &Field);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H