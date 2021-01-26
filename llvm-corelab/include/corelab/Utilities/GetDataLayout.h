#include "llvm/IR/Value.h"
#include "llvm/IR/DataLayout.h"

namespace corelab {

  using namespace llvm;

  //made by juhyun 15/9/24
  // get (or initialize) DataLayout from Value 
  DataLayout *getDataLayout(Value *v);

  const Module *getModuleFromVal(const Value *V);

}
