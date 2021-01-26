//by changsu
#ifndef GET_GEP_CHAIN
#define GET_GEP_CHAIN

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <list>

using namespace llvm;
using namespace std;

namespace corelab {
	Value *getMemRelatedOper(Instruction *);
	void collectChainInst(Value *, list<Instruction *> &);
	list<Instruction *> getGEPChain(Instruction *);
	list<Instruction *> sortInstructions(list<Instruction *> &);
}

#endif
