#include "corelab/Utilities/IsVolatile.h"

using namespace llvm;

bool corelab::isVolatile(const Instruction *inst) {

  if(const LoadInst *load = dyn_cast<LoadInst>(inst))
    return load->isVolatile();

  if(const StoreInst *store = dyn_cast<StoreInst>(inst))
    return store->isVolatile();

  return false;
}
