#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "corelab/CorelabHLS/ParallelCallSitePrivatization.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>

using namespace llvm;
using namespace corelab;

//const static bool debug = true;

char ParallelCallSitePrivatization::ID = 0;
static RegisterPass<ParallelCallSitePrivatization> X("parallel-callsite-privatization", 
		"Parallelizable CallSite Privatization", false, false);

void ParallelCallSitePrivatization::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.setPreservesAll();
}

	unsigned ParallelCallSitePrivatization::getMetadata(Instruction *inst) {
		MDNode *md = inst->getMetadata("namer");
		assert(md);
		Metadata *m  = md->getOperand(0).get();
		ValueAsMetadata *VMD = dyn_cast<ValueAsMetadata>(m);
		assert(VMD);
		Value *v = VMD->getValue();
		assert(v);

		ConstantInt *cInt = dyn_cast<ConstantInt>(v);
		assert(cInt);

		return cInt->getZExtValue();
	}

	void ParallelCallSitePrivatization::makeMetadataParallel(Instruction* instruction) {
		LLVMContext &context = instruction->getModule()->getContext();
		Constant* IdV = ConstantInt::get(Type::getInt64Ty(context), 1);
		Metadata* IdM = (Metadata*)ConstantAsMetadata::get(IdV);
		Metadata* valuesArray[] = {IdM};
		ArrayRef<Metadata *> values(valuesArray, 1);
		MDNode* mdNode = MDNode::get(context, values);
		NamedMDNode *namedMDNode = module->getOrInsertNamedMetadata("corelab.parallel");
		namedMDNode->addOperand(mdNode);
		instruction->setMetadata("parallel", mdNode);
	}


	void ParallelCallSitePrivatization::makeMetadata(Instruction* instruction, uint64_t Id) {
		LLVMContext &context = instruction->getModule()->getContext();
		Constant* IdV = ConstantInt::get(Type::getInt64Ty(context), Id);
		Metadata* IdM = (Metadata*)ConstantAsMetadata::get(IdV);
		Metadata* valuesArray[] = {IdM};
		ArrayRef<Metadata *> values(valuesArray, 1);
		MDNode* mdNode = MDNode::get(context, values);
		NamedMDNode *namedMDNode = module->getOrInsertNamedMetadata("corelab.namer");
		namedMDNode->addOperand(mdNode);
		instruction->setMetadata("namer", mdNode);
	}


void ParallelCallSitePrivatization::privatizeCallInst(CallInst *callInst) {
	Function *originFunction = callInst->getCalledFunction();
	assert(originFunction);

	ValueToValueMapTy VMap;
	Function *newFunction = CloneFunction(originFunction, VMap);

	string func_name = newFunction->getName().str();
	unsigned i = 0;
	for ( auto ch : func_name )
	{
		if ( (ch) == '.' || (ch) == '-' )
			func_name.replace(i,1,"_");
		i++;
	}
	StringRef new_name(func_name);

	newFunction->setName(new_name);

	///////////
//	funcSet.insert(newFunction);
//	parallelFunctionSet.insert(newFunction);
	buildCloneFunctionFromParallel(newFunction, func2origin2clone[newFunction]);
	changeOriginToClone(func2origin2clone[newFunction],
																callInst->getFunction(), newFunction);
	///////////

	unsigned numOperand = callInst->getNumArgOperands();
	std::vector<Value *> actuals(0);
	actuals.clear();
	actuals.resize(numOperand);

	for ( unsigned i = 0; i < numOperand; i++ )
		actuals[i] = callInst->getArgOperand(i);

	CallInst *newCallInst = CallInst::Create(newFunction, actuals, "", callInst);
	callInst->replaceAllUsesWith(newCallInst);

	unsigned callId = getMetadata(callInst);
	makeMetadata(newCallInst, callId);

	makeMetadataParallel(newCallInst);
}

void ParallelCallSitePrivatization::apply2Function(Function *func) {
	set<CallInst *> callInstSet;
	callInstSet.clear();

	for ( auto bi = func->begin(); bi != func->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) )
				callInstSet.insert(callInst);

	//if the func is in func2origin2clone ( the func is recently generated for privatization )
	//, normal call site (including nested...) should be redirected to clone function.
	// and the clone functio should be added in the funcSet

	for ( auto callInst : callInstSet )
	{
		unsigned callId = getMetadata(callInst);
		
		bool isParallel = false;
		for ( auto idIter : parallelSet )
			if ( idIter == callId )
				isParallel = true;
		
		if ( isParallel )
			privatizeCallInst(callInst);
	}

	for ( auto callInst : callInstSet )
	{
		unsigned callId = getMetadata(callInst);
		
		bool isParallel = false;
		for ( auto idIter : parallelSet )
			if ( idIter == callId )
				isParallel = true;
		
		if ( isParallel )
			callInst->eraseFromParent();
	}
}

void ParallelCallSitePrivatization::buildCloneFunctionFromParallel(
													Function *func, DenseMap<Function *, Function *> &cloneMap) {
	for ( auto bi = func->begin(); bi != func->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) ) {
				Function *originFunction = callInst->getCalledFunction();
				assert(func);

				unsigned callId = getMetadata(callInst);
				for ( auto idIter : parallelSet )
					if ( idIter == callId )
						continue;

				if ( cloneMap.count(originFunction) )
					continue;

				ValueToValueMapTy VMap;
				Function *newFunction = CloneFunction(originFunction, VMap);

				string func_name = newFunction->getName().str();
				unsigned i = 0;
				for ( auto ch : func_name )
				{
					if ( (ch) == '.' || (ch) == '-' )
						func_name.replace(i,1,"_");
					i++;
				}
				StringRef new_name(func_name);
				newFunction->setName(new_name);

				cloneMap[originFunction] = newFunction;

				buildCloneFunctionFromParallel(originFunction, cloneMap);
			}
}

void ParallelCallSitePrivatization::changeOriginToClone(
																								DenseMap<Function *, Function *> &cloneMap,
																								Function *parentFunc, Function *newFunc) {
	funcSet.insert(newFunc);
	func2caller[newFunc].insert(parentFunc);
	
	for ( auto bi = newFunc->begin(); bi != newFunc->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) ) {
				Function *calledFunction = callInst->getCalledFunction();
				assert(calledFunction);

				unsigned callId = getMetadata(callInst);
				for ( auto idIter : parallelSet )
					if ( idIter == callId ) 
						continue; // parallel callsite will be handled in other function

				if ( !cloneMap.count(calledFunction) )
					assert(0);

				Function *cloneFunction = cloneMap[calledFunction];

				unsigned numOperand = callInst->getNumArgOperands();
				std::vector<Value *> actuals(0);
				actuals.clear();
				actuals.resize(numOperand);

				for ( unsigned i = 0; i < numOperand; i++ )
					actuals[i] = callInst->getArgOperand(i);

				CallInst *newCallInst = CallInst::Create(cloneFunction, actuals, "", callInst);
				callInst->replaceAllUsesWith(newCallInst);

				makeMetadata(newCallInst, callId);

				changeOriginToClone(cloneMap, newFunc, cloneFunction);
			}
}
 

void ParallelCallSitePrivatization::setAllFunction() {
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		if ( (&*fi)->isDeclaration() ) continue;

		funcSet.insert(&*fi);
		func2done[&*fi] = false;
	}

	for ( auto func : funcSet )
		for ( auto bi = func->begin(); bi != func->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
				if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) ) {
					Function *callee = callInst->getCalledFunction();
					assert(callee);

					(func2caller[callee]).insert(func);
				}

	/*
	for ( auto callInst : parallelCallSiteSet )
	{
		Function *func = callInst->getCalledFunction();
		parallelFunctionSet.insert(func);
	}*/
}

void ParallelCallSitePrivatization::apply2AllFunction() {
	seed = 1; //for getting next function in round robin way

	Function *nextFunction = module->getFunction("main");
	func2done[nextFunction] = true;
	while (1) {
		if ( nextFunction == NULL )
			break;

		if ( isReady(nextFunction) ) {
			apply2Function(nextFunction);
			func2done[nextFunction] = true;
		}

		nextFunction = getNextFunction();
	}
}

bool ParallelCallSitePrivatization::runOnModule(Module& M) {
	module = &M;

	if ( findParallelCallSite() ) {
		errs() << "Parallel callsites : \n";
		for ( auto callInst : parallelCallSiteSet )
			callInst->dump();
	}
	else {
		errs() << "Can not find 'callsite_parallel.debug' file\n";
		assert(0);
	}

	setAllFunction();
	apply2AllFunction();

	return true;
}

bool ParallelCallSitePrivatization::findParallelCallSite() {
	string fileName = "callsite_parallel.debug";

	ifstream openFile(fileName.data());
	if ( openFile.is_open() ) {
		string line;
		while(getline(openFile, line)) {
			int id = std::stoi(line);	
			parallelSet.insert(id);
		}
	}
	else
		return false;

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
				if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) ) {
					unsigned id = getMetadata(callInst);
					
					bool isParallel = false;
					for ( auto idIter : parallelSet )
						if ( idIter == id )
							isParallel = true;

					if ( isParallel )
						parallelCallSiteSet.insert(callInst);
				}

	assert( parallelCallSiteSet.size() == parallelSet.size() );

	return true;
}

