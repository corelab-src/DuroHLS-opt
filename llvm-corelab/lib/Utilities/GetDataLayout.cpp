#include "corelab/Utilities/GetDataLayout.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace corelab {

  using namespace llvm;

  // get (or initialize) DataLayout from Value 
  DataLayout *getDataLayout(Value *V){

    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
      return (DataLayout *)&GV->getParent()->getDataLayout();
        
    else if (Instruction *I = dyn_cast<Instruction>(V))
      return (DataLayout *)&I->getModule()->getDataLayout();
    
    else if (Argument *A = dyn_cast<Argument>(V))
      return (DataLayout *)&A->getParent()->getParent()->getDataLayout();

    else{
      assert( false && "ERROR:: getDataLayout return NULL. because type of V doesn't registered yet");
      return NULL;
    }
  }

  const Module *getModuleFromVal(const Value *V) {
    if (const Argument *MA = dyn_cast<Argument>(V))
      return MA->getParent() ? MA->getParent()->getParent() : nullptr;

    if (const BasicBlock *BB = dyn_cast<BasicBlock>(V))
      return BB->getParent() ? BB->getParent()->getParent() : nullptr;

    if (const Instruction *I = dyn_cast<Instruction>(V)) {
      const Function *M = I->getParent() ? I->getParent()->getParent() : nullptr;
      return M ? M->getParent() : nullptr;
    }

    if (const GlobalValue *GV = dyn_cast<GlobalValue>(V))
      return GV->getParent();

    if (const auto *MAV = dyn_cast<MetadataAsValue>(V)) {
      for (const User *U : MAV->users())
        if (isa<Instruction>(U))
          if (const Module *M = getModuleFromVal(U))
            return M;
      return nullptr;
    }

    return nullptr;
  }


}
