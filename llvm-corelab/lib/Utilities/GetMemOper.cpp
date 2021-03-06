#include "corelab/Utilities/GetMemOper.h"

using namespace llvm;

const Value *corelab::getMemOper(const Instruction *inst) {
  return getMemOper(const_cast<Instruction *>(inst));
}

Value *corelab::getMemOper(Instruction *inst) {

  if(LoadInst *load = dyn_cast<LoadInst>(inst))
    return load->getPointerOperand();

  if(StoreInst *store = dyn_cast<StoreInst>(inst))
    return store->getPointerOperand();

  if(isa<MemIntrinsic>(inst))
    return NULL;

  if(isa<VAArgInst>(inst))
    assert(false && "Variadic arguments not supported");

  return NULL;
}

void corelab::setMemOper(Instruction *inst, Value *value) {

  if(isa<LoadInst>(inst)) {
    inst->setOperand(0, value);
    return;
  }

  if(isa<StoreInst>(inst)) {
    inst->setOperand(1, value);
    return;
  }

  if(isa<MemIntrinsic>(inst) || isa<VAArgInst>(inst))
    assert(false && "Unimplemented");

  assert(false && "No memory operator");
}
