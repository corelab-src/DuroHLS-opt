#ifndef GETSIZE_H
#define GETSIZE_H

#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/DataLayout.h"

namespace corelab {

	using namespace llvm;

  // Conservatively return the size of a value
  unsigned getSize(Type *type, const DataLayout *TD);
  unsigned getSize(const Value *value);
  unsigned getTargetSize(const Value *value);
}

#endif /* GETSIZE_H */
