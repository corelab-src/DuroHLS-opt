#include "corelab/Utilities/GetMemOper.h"
#include "corelab/Utilities/GetGEPChain.h"

#include <vector>

using namespace llvm;
using namespace std;

namespace corelab {

	Value *getMemRelatedOper(Instruction *inst) {
		for ( int i = 0; i < inst->getNumOperands(); i++ )
		{
			Value *opV = inst->getOperand(i);
			assert(opV);
			Type *ty = opV->getType();
			if ( isa<PointerType>(ty) )
				return opV;
			if ( PHINode *phiInst = dyn_cast<PHINode>(opV) )
				return opV;
			//A[i+1] case
			if ( PHINode *phiInst = 
					dyn_cast<PHINode>(dyn_cast<User>(opV)->getOperand(0)) )
				return opV;
		}
		return NULL;
	}

	//This Function's ordering follows
	//the listing of instructions in source.ll
	list<Instruction *> sortInstructions(list<Instruction *> &chain) {
		list<Instruction *> newList;

		Function *F = chain.back()->getParent()->getParent();
		for ( auto bi = F->begin(); bi != F->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;
				
				for ( auto chainIter : chain )
					if ( chainIter == inst ) 
						newList.push_back(inst);
			}
		return newList;
	}

	//XXX:non-interprocedural
	void collectChainInst(Value *v, std::list<Instruction *> &chain) {
		if ( Instruction *inst = dyn_cast<Instruction>(v) ) {
			if ( GetElementPtrInst *gepInst = dyn_cast<GetElementPtrInst>(inst) ) {
				chain.push_front(inst);
				Value *ptrOperand = gepInst->getOperand(0);
				Type *ptrType = ptrOperand->getType();

				// GEP ptrV 0 1dim 2dim 3dim ...
				if ( isa<ArrayType>(dyn_cast<PointerType>(ptrType)->getElementType()) ) {
					for ( int i = inst->getNumOperands()-1; i > 1; --i )
						collectChainInst(inst->getOperand(i), chain);
				}
				// GEP ptrV offset
				else
					collectChainInst(inst->getOperand(1), chain);

				collectChainInst(ptrOperand, chain);
			}
			else if ( PHINode *phiInst = dyn_cast<PHINode>(inst) ) {
				chain.push_front(inst);
			}
			//Adder ( indV )
			else {
				chain.push_front(inst);
				Value *ptrOperand = getMemRelatedOper(inst);
				assert(ptrOperand);
				collectChainInst(ptrOperand, chain);
			}
		}
		//else such as argument, memObj itself
	}

	list<Instruction *> getGEPChain(Instruction *inst) {
		if ( isa<StoreInst>(inst) || isa<LoadInst>(inst) ) {
			Value *ptrV = getMemOper(inst);
			list<Instruction *> instList;
			instList.clear();

			collectChainInst(ptrV, instList);
			instList.push_back(inst);
			return sortInstructions(instList);
		}
		else
			assert(0 && "getGEPChain : No Memory Instruction\n");
	}

}
