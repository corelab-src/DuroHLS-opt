//Written by csKim
#ifndef LLVM_CORELAB_PABASEDAAOPT_H
#define LLVM_CORELAB_PABASEDAAOPT_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/DenseMap.h"

#include "corelab/Analysis/PADriver.h"

#include <set>
#include <list>
#include <vector>
#include <iostream>
#include <fstream>

namespace corelab
{
	using namespace llvm;
	using namespace std;

	class LoopNode_
	{
		public:
			LoopNode_(const Loop *L_, bool innerMost_) : L(L_), innerMost(innerMost_) {}
			
			//-----------Loop Usage------------//
			const Loop *getLoop() { return L; }

			bool isInnerMost() { return innerMost; }
			bool isSimpleForm() { return simpleForm; }
			bool hasSimpleCanonical() { return simpleCanonical; }
			bool hasSimpleExitCond() { return simpleExitCond; }

			PHINode *getInductionVariable() { return indvar; }
			Instruction *getStride() { return stride; }
			Value *getInitValue() { return initValue; }
			Instruction *getExitCondition() { return exitCondition; }
			BranchInst *getExitBranch() { return exitBranch; }

			//-----------Loop Build------------//
			void setLoopInfo(void);
			bool setCanonicalInductionVariableAux(const Loop *);
			bool setExitCondition(const Loop *);

		private:
			const Loop *L;
			bool innerMost;
			
			//Loop does not contain exit block
			bool simpleForm; //simple form && # of exit block == 1
			bool simpleCanonical;
			bool simpleExitCond;

			PHINode *indvar;
			Instruction *stride;
			Value *initValue;
			Instruction *exitCondition;
			BranchInst *exitBranch;
	};

	class PABasedAAOPT 
	{
		public:
			PABasedAAOPT(Module *module_, PADriverTest *pa_, 
				DenseMap<const Function *, LoopInfo *> loopInfoOf_) :
				module(module_), pa(pa_), loopInfoOf(loopInfoOf_) {}
			~PABasedAAOPT();

			//-----------Loop Usage------------//
			//memInst A , B in Loop L
			//					LoopNode LN, ptr of A, access size, ptr of B, access size
			//size as byte
			// !!!  ITERATION DISTANCE !!!
			pair<bool, int> getDistance(LoopNode_ *, Value *, int, Value *, int);
			//example : unknown value
			// return false, -1 ( always alias )
			//example : known value
			// return false, 0 ( always not alias = dosen't have distance )
			//example : prt A[i] , ptr B[i+1] in the same iteration 
			// return true, 1 ( A will access B in the next(1) iter )
			//example : ptr A[i+1] , ptr B[i] in the same iteration
			// return true, -1 ( A accesses B in the previous iter )

			list<LoopNode_ *> getLoopNodes() { return loopNodeList; }
			LoopNode_ *getLoopNode(const Loop *L) { 
				if ( loop2node.count(L) )
					return loop2node[L];
				else
					return NULL;
			}

			pair<bool, int> distanceCheck(LoopNode_ *,
					list<pair<int, list<Value *>>>, int, list<pair<int, list<Value *>>>, int);
			bool hasTargetIndInFirst(Value *,list<pair<int, list<Value *>>>);

			void initLoop(const Loop *, bool);
			void initLoops(const Loop *);
			//need to call before use
			void initLoopAA(void);

			//-----------Usage------------//
			//memInst A , B : 
			//					ptr of A, access size, ptr of B, access size
			//size as byte
			bool isNoAlias(Value *, int, Value *, int);

			int getAccessSize(Instruction *inst) {
				Type *bitWidthType;
				if ( LoadInst *lInst = dyn_cast<LoadInst>(inst) )
					bitWidthType = lInst->getType();
				else if ( StoreInst *sInst = dyn_cast<StoreInst>(inst) )
					bitWidthType = sInst->getOperand(0)->getType();
				else
					assert(0);

				if ( IntegerType *intTy = dyn_cast<IntegerType>(bitWidthType) )
					return intTy->getBitWidth() /8;
				else if ( isa<PointerType>(bitWidthType) )
					return 4;
				else if ( bitWidthType->isFloatTy() )
					return 4;
				else if ( bitWidthType->isDoubleTy() )
					return 8;
				else
					return 8;
			}

			//-----------Build------------//
			void collectPointers(list<Value *>&, Value *);
			Value *getSamePoint(list<Value *>, list<Value *>);
			list<Value *> trimList(list<Value *>, Value *);
			bool collectOperations(list<pair<int, list<Value *>>>&, Value *);
			bool collectOperationsAux(list<pair<int, list<Value *>>>&, int, list<Value *>&, Value *);
			bool getInductionInc(PHINode *, pair<Value *, Value *>&);
			bool offsetAliasCheckLoopInvariant(
					list<pair<int, list<Value *>>>, int, list<pair<int, list<Value *>>>, int);
			void collectOffsetList(list<pair<int, list<Value *>>>, list<int> &);

		private:
			Module *module;
			PADriverTest *pa;
			DenseMap<const Function *, LoopInfo *> loopInfoOf;

			list<LoopNode_ *> loopNodeList;
			DenseMap<const Loop *, LoopNode_ *> loop2node;
	};
}

#endif
