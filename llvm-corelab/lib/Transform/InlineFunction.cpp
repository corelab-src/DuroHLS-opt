#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/IR/Attributes.h"

#include "corelab/Transform/InlineFunction.h"


#include <string>
#include <fstream>
#include <iostream>

using namespace llvm;
using namespace corelab;

const static bool debug = true;

char InlineSmallFunction::ID = 0;
static RegisterPass<InlineSmallFunction> X("inline-small", 
		"InlineSmallFunction", false, false);

cl::opt<unsigned> Threshold(
		"threshold", cl::init(30), cl::NotHidden,
		cl::desc("Define Threshold to inline functions"));

cl::opt<unsigned> LargeThreshold(
		"large-threshold", cl::init(100), cl::NotHidden,
		cl::desc("Define Threshold for large state check"));

cl::opt<unsigned> OptSizeThreshold(
		"opt-size-threshold", cl::init(50), cl::NotHidden,
		cl::desc("Define Threshold for opt size attr"));


DenseMap<Function *, unsigned> func2loccopy;

bool compare_functions(Function *first, Function *second) {
	return ( func2loccopy[first] < func2loccopy[second] );
}

void InlineSmallFunction::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.addRequired< LoopInfoWrapperPass >();
	AU.setPreservesAll();
}

void InlineSmallFunction::setAttributeSize(Function *func) {
	if ( !isOptNone(func) ) {
		if (debug) errs() << "OptSize : " << func->getName() << "\n";
		func->addAttribute(AttributeList::AttrIndex::FunctionIndex, Attribute::OptimizeForSize);
	}
}

void InlineSmallFunction::smallFunctionSearch() {
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		Function *func = &*fi;

		if ( func->isDeclaration() || recursiveCheck(func) )
			continue;

		unsigned instructionCount = 0;

		//Naive Implementation
		for ( auto bi = func->begin(); bi != func->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
				instructionCount++;

		func2loc[func] = instructionCount;

		if ( instructionCount < Threshold ) 
			smallFunctions.push_back(func);
		else if ( uniqueFunctionSearch(func) ) {
			if (debug) errs() << "unique ";
			smallFunctions.push_back(func);
		}

		if (debug) errs() << "search : " << func->getName() << " : " << instructionCount << " : " << getStateCount(func) << "\n";


		if ( OptSizeThreshold < getStateCount(func) ) {
			setAttributeSize(func);

			/*
			AttributeList AS = func->getAttributes();
			AttrBuilder FnAttr(AS.getFnAttributes());
			AS = AS.addAttributes(module->getContext(), Attribute::OptimizeNone,
														AttributeSet::get(module->getContext(), FnAttr));
			func->setAttributes(AS);*/

			//TODO: Need to Check profitable loops
			//XXX: blocking unrolling is not enough to stop oversizing
			/*
			if (debug) errs() << "Block Unrolling : " << func->getName() << "\n";

			LoopInfo *li = 
				new LoopInfo(std::move(getAnalysis< LoopInfoWrapperPass >(*func).getLoopInfo()));
			std::vector<Loop *> loops( li->begin(), li->end() );
			for ( auto iter : loops ) {
				iter->setLoopAlreadyUnrolled();
			}
			*/
		}
	}

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				if ( CallInst *cb = dyn_cast<CallInst>(&*ii) ) {
					if ( Function *callee = cb->getCalledFunction() ) {
						for ( auto targets : smallFunctions )
							if ( targets == callee )
								func2callsite[callee].push_back(&*ii);
					}
				}
				else if ( InvokeInst *cb = dyn_cast<InvokeInst>(&*ii) ) {
					if ( Function *callee = cb->getCalledFunction() ) {
						for ( auto targets : smallFunctions )
							if ( targets == callee )
								func2callsite[callee].push_back(&*ii);
					}
				}
			}
}

unsigned InlineSmallFunction::getStateCount(Function *func) {
	unsigned naiveStateCount = 0;
	float naiveFloat = 0;
	for ( auto bi = func->begin(); bi != func->end(); )
	{
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
		{
			Instruction *inst = &*ii;
			if ( isa<CallInst>(inst) || isa<InvokeInst>(inst) )
				naiveFloat = naiveFloat + 2;
			else if ( isa<StoreInst>(inst) || isa<LoadInst>(inst) )
				naiveFloat = naiveFloat + 1;
		}

		if ( ++bi != func->end() )
			naiveFloat = naiveFloat + 2;
	}

	naiveStateCount = (naiveFloat/2);
	return naiveStateCount;
}


bool InlineSmallFunction::largeStateCheck(Function *func) {

	if ( LargeThreshold < getStateCount(func) )
		return true;
	else
		return false;
}

bool InlineSmallFunction::enoughSmall(Function *func) {
	if ( (getStateCount(func) < 10) && (func2loc[func] < Threshold) )
		return true;
	else 
		return false;
}

bool InlineSmallFunction::isNoInline(Function *func) {
	//NoInline
//	return (func->getFnAttribute("noinline").getValueAsString() == "true");
	return func->hasFnAttribute(Attribute::NoInline);
}

bool InlineSmallFunction::isOptNone(Function *func) {
	//NoInline
	return func->hasFnAttribute(Attribute::OptimizeNone);
}


bool InlineSmallFunction::checkInlinePossible(Function *targets) {
	unsigned thisFunctionLOC = func2loc[targets];

	DenseMap<Function *, unsigned> caller2numofcall;
	caller2numofcall.clear();
	bool tooLarge = false;
	bool callerLargeState = false;
	bool calleeLargeState = false;
	bool noinline = false;

	for ( auto callsites : func2callsite[targets] )
	{
		Function *caller = callsites->getFunction();
		unsigned callernum = func2loc[caller];

//		if (debug) errs() << "callee : " << targets->getName() << " : " << thisFunctionLOC << "\n";
//		if (debug) errs() << "caller : " << caller->getName() << " : " << callernum<< "\n\n";
		if (debug) errs() << "callee : "<<targets->getName()<<" : "<<getStateCount(targets)<<"\n";
		if (debug) errs() << "caller : "<<caller->getName()<<" : "<<getStateCount(caller)<<"\n\n";

		if ( caller2numofcall.count(caller) )
			caller2numofcall[caller] = caller2numofcall[caller] + 1;
		else
			caller2numofcall[caller] = 1;
	}

	for ( auto iter : caller2numofcall )
	{
		Function *caller = iter.first;
		unsigned numofcall = iter.second;
		unsigned callernum = func2loc[caller];

		bool callerSmall = enoughSmall(caller) && (numofcall < 3);
		bool calleeSmall = enoughSmall(targets) && (numofcall < 3);

		if ( !callerSmall && !calleeSmall ) {
		if ( LargeThreshold < ((getStateCount(targets) * numofcall) + getStateCount(caller)) ) {
			tooLarge = true;
			if (debug) {
				errs() <<  "caller(" << caller->getName()<<") : " << getStateCount(caller) << " ";
				errs() << "and callee("<<targets->getName()<<") : " << getStateCount(targets) << " ";
				errs() << "has Large State already\n";
			}

//			setAttributeSize(targets);
//			setAttributeSize(caller);
			break;
		}
		}

//		if ( largeStateCheck(caller) && (!enoughSmall(targets) || !(numofcall < 3)) ) {
		if ( largeStateCheck(caller) && !calleeSmall ) {
			callerLargeState = true;
			if (debug) {
				errs() <<  targets->getName() << "\t's caller (" << caller->getName() << ")\t";
				errs() << "has Large State already\n";
			}

//			setAttributeSize(caller);
			break;
		}

//		if ( largeStateCheck(targets) && (!enoughSmall(caller) || !(numofcall < 3)) ) {
		if ( largeStateCheck(targets) && !callerSmall ) {
			calleeLargeState = true;
			if (debug) {
				errs() <<  targets->getName() << "\tcallee ";
				errs() << "has Large State already\n";
			}

//			setAttributeSize(targets);
			break;
		}

		if ( isNoInline(targets) ) {
			noinline = true;
			if (debug) {
				errs() <<  targets->getName() << "\tcallee ";
				errs() << "has noinline attribute\n";
			}
		}
		
	}

	return !tooLarge & !callerLargeState & !calleeLargeState & !noinline;

//	return !callerLargeState & !calleeLargeState;
}

void InlineSmallFunction::updateLOC(Function *caller) {
	unsigned instructionCount = 0;
	for ( auto bi = caller->begin(); bi != caller->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			instructionCount++;
	func2loc[caller] = instructionCount;
}


bool InlineSmallFunction::uniqueFunctionSearch(Function *calleeCheck) {
	unsigned callnum = 0;

	for ( auto fii = module->begin(); fii != module->end(); fii++ )
		for ( auto bi = (&*fii)->begin(); bi != (&*fii)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				if (  CallInst *cb = dyn_cast<CallInst>(&*ii) ) {
					if ( Function *callee = cb->getCalledFunction() ) {
						if ( calleeCheck == callee ) {
							callnum++;
						}
					}
				}
				else if ( InvokeInst *cb = dyn_cast<InvokeInst>(&*ii) ) {
					if ( Function *callee = cb->getCalledFunction() ) {
						if ( calleeCheck == callee ) {
							callnum++;
						}
					}
				}
			}

	if ( callnum == 1 ) 
		return true;
	else
		return false;
}

void InlineSmallFunction::makeInline() {
	bool isChanged = false;
	for ( auto targets : smallFunctions )
	{
		if (debug) errs() << "Inline Try : " << targets->getName() << "\n";

		if ( !checkInlinePossible(targets) ) {
			if (debug) errs() << "Not Possible\n\n";
			continue;
		}

		for ( auto callsites : func2callsite[targets] )
		{
			assert(callsites);
			Function *caller = callsites->getFunction();
			assert(caller);
			
			InlineFunctionInfo IFI;
			CallSite cs(callsites);
			if ( InlineFunction(cs, IFI) ) {
				if (debug) errs() << "succ";
			}
			else {
				if (debug) errs() << "fail";
			}
			

			updateLOC(caller);
			if (debug) errs() << "\n";
		}
		if (debug) errs() << "\n";
	}
}

//bool InlineSmallFunction::runOnFunction (Function &F) {
//	
//}

bool InlineSmallFunction::recursiveCheck(Function *func) {
	bool hasRecur = false;
	for ( auto bi = (func)->begin(); bi != (func)->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
		{
			if (  CallInst *cb = dyn_cast<CallInst>(&*ii) ) {
				if ( Function *callee = cb->getCalledFunction() ) 
					if ( func == callee ) {
						hasRecur = true;
					}
			}
			else if ( InvokeInst *cb = dyn_cast<InvokeInst>(&*ii) ) {
				if ( Function *callee = cb->getCalledFunction() ) 
					if ( func == callee ) {
						hasRecur = true;
					}
			}
		}

	if ( hasRecur )
		if (debug) errs() << " Recursive Function exist : " << func->getName() << "\n";

	return hasRecur;
}

bool InlineSmallFunction::runOnModule(Module& M) {
	if (debug)
		errs() << "\n@@@@@@@@@@ Inline Transformation Start @@@@@@@@@@@@@@\n";

	module = &M;

	smallFunctionSearch();

	if (debug) errs() << "\n";

	func2loccopy.clear();
	func2loccopy = func2loc;
	smallFunctions.sort(compare_functions);

	makeInline();

	if (debug) errs() << "\n@@@@@@@@@@ Inline Transformation END @@@@@@@@@@@@@@\n";

	return true;
}


