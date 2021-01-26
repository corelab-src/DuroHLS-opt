#ifndef LLVM_LIBERTY_REC2ITER
#define LLVM_LIBERTY_REC2ITER


#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"

namespace corelab {
  using namespace llvm;

  /* A pass which turns a recursive function into an iterative function.
   */
  class Rec2Iter : public ModulePass {

    bool runOnFunction(Function &);
    void demoteAllocasToHeap(Function &);

  public:

    static char ID;
    Rec2Iter()
      : ModulePass(ID) {}

    void getAnalysisUsage(AnalysisUsage &au) const
    {
    }

    bool runOnModule(Module &);

    StringRef getPassName() const { return "Rec2Iter"; }

  };
}

#endif // LLVM_LIBERTY_REC2ITER
