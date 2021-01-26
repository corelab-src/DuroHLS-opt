
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "corelab/CorelabHLS/CallSitePrivatization.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace llvm;
using namespace corelab;

const static bool debug = true;

char CallSitePrivatization::ID = 0;
static RegisterPass<CallSitePrivatization> X("callsite-privatization", 
		"CallSite Privatization", false, false);

void CallSitePrivatization::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.setPreservesAll();
}

	unsigned CallSitePrivatization::getMetadata(Instruction *inst) {
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

	void CallSitePrivatization::makeMetadata(Instruction* instruction, uint64_t Id) {
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


void CallSitePrivatization::privatizeCallInst(CallInst *callInst) {
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
	funcSet.insert(newFunction);
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
}

void CallSitePrivatization::apply2Function(Function *func) {
	set<CallInst *> callInstSet;
	callInstSet.clear();

	for ( auto bi = func->begin(); bi != func->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) )
				callInstSet.insert(callInst);

	for ( auto callInst : callInstSet )
		privatizeCallInst(callInst);

	for ( auto callInst : callInstSet )
		callInst->eraseFromParent();
}

void CallSitePrivatization::setAllFunction() {
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
}

void CallSitePrivatization::apply2AllFunction() {
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

bool CallSitePrivatization::runOnModule(Module& M) {
	module = &M;

	setAllFunction();
	apply2AllFunction();

	return true;
}


