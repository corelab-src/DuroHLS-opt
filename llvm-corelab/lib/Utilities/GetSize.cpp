#include <stdio.h>
#include "llvm/IR/Module.h"
#include "corelab/Utilities/GetDataLayout.h"
#include "corelab/Utilities/GetSize.h"
#include "llvm/IR/DerivedTypes.h"



namespace corelab {

	using namespace llvm;

  unsigned getSize(Type *type, const DataLayout *TD) {
    if(type->isSized()) {
      return TD ? TD->getTypeStoreSize(type) : ~0u;
    }
    return 0;
  }

  // Conservatively return the size of a value
  unsigned getSize(const Value *value) {
    Type *type = value->getType();
		const Module *M = getModuleFromVal(value);

    return getSize(type, &M->getDataLayout());
  }

  unsigned getTargetSize(const Value *value) {
    Type *type = value->getType();

    SequentialType *seqType = dyn_cast<SequentialType>(type);
//    assert(seqType && "Must be a SequentialType");
		if (!seqType)
			return 0;

    Type *targetType = seqType->getElementType();

		const Module *M = getModuleFromVal(value);

    return getSize(targetType,&M->getDataLayout());
  }
}
