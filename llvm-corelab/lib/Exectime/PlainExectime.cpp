#include <stdio.h>
#include <fstream>
#include <iostream>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/CallSite.h"

#include "corelab/Utilities/GlobalCtors.h"

#include "corelab/Exectime/PlainExectime.h"

using namespace corelab;

char PlainExectime::ID = 0;
static RegisterPass<PlainExectime> X("plain-exectime", "execution time check for plain exe", false, false);

cl::opt<bool> ACCUM(
		"accumulation", cl::init(false), cl::NotHidden,
		cl::desc("Accumulated Execution Time"));

static int funcID;

static void initFuncID(void) {
	funcID = 0;
}

static int generateFuncID(void) {
	return ++funcID;
}

void PlainExectime::setFunctions(Module &M)
{
	LLVMContext &Context = M.getContext();

	plainExecInitialize = M.getOrInsertFunction(
			"plainExecInitialize",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context));

	plainExecFinalize = M.getOrInsertFunction(
			"plainExecFinalize",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context));

	plainExecCallSiteBegin = M.getOrInsertFunction(
			"plainExecCallSiteBegin",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context));

	plainExecCallSiteEnd = M.getOrInsertFunction(
			"plainExecCallSiteEnd",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context));
}

void PlainExectime::setFunctionID(Module& M) {
	for ( auto fi = M.begin(); fi != M.end(); fi++ )
	{
		Function *func = &*fi;

		if ( func->isDeclaration() ) continue;
		if ( !func2ID.count(func) )
			func2ID[func] = generateFuncID();
	}
}

void PlainExectime::setIniFini(Module& M)
{
	LLVMContext &Context = M.getContext();
	std::vector<Type*> formals(0);
	std::vector<Value*> actuals(0);
	FunctionType *voidFcnVoidType = FunctionType::get(Type::getVoidTy(Context), formals, false);

	/* initialize */
	Function *initForCtr = Function::Create(
			voidFcnVoidType, GlobalValue::InternalLinkage, "__constructor__", &M); 
	BasicBlock *entry = BasicBlock::Create(Context,"entry", initForCtr); 
	BasicBlock *initBB = BasicBlock::Create(Context, "init", initForCtr); 
	actuals.resize(2);
	actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), func2ID[M.getFunction("main")]);
	actuals[1] = ConstantInt::get(Type::getInt32Ty(Context), funcID);

	CallInst::Create(plainExecInitialize, actuals, "", entry); 
	BranchInst::Create(initBB, entry); 
	ReturnInst::Create(Context, 0, initBB);
	callBeforeMain(initForCtr);

	/* finalize */
	Function *finiForDtr = Function::Create(voidFcnVoidType, GlobalValue::InternalLinkage, "__destructor__",&M);
	BasicBlock *finiBB = BasicBlock::Create(Context, "entry", finiForDtr);
	BasicBlock *fini = BasicBlock::Create(Context, "fini", finiForDtr);

	actuals.resize(3);
	actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), func2ID[M.getFunction("main")]);
	actuals[1] = ConstantInt::get(Type::getInt32Ty(Context), funcID);
	actuals[2] = ACCUM ? ConstantInt::get(Type::getInt32Ty(Context), 1) :
						ConstantInt::get(Type::getInt32Ty(Context), 0);


	CallInst::Create(plainExecFinalize, actuals, "", fini);
	BranchInst::Create(fini, finiBB);
	ReturnInst::Create(Context, 0, fini);
	callAfterMain(finiForDtr);
}


void PlainExectime::insertToCallSite(Module& M) {
	LLVMContext &Context = M.getContext();

	//insert begin
	for ( auto fi = M.begin(); fi != M.end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;
				Function *callee = NULL;
				if ( CallInst *cInst = dyn_cast<CallInst>(inst) ) {
					callee = cInst->getCalledFunction();
				}
				else if ( InvokeInst *iInst = dyn_cast<InvokeInst>(inst) ) {
					callee = iInst->getCalledFunction();
				}

				if ( isa<CallInst>(inst) || isa<InvokeInst>(inst) ) {
					assert(callee != NULL);

					if ( callee->isDeclaration() ) continue;

					insertList[inst] = callee;
				}
			}

	for ( auto iter : insertList ) {
		std::vector<Value *> args(0);
		args.resize(2);
		args[0] = ConstantInt::get(Type::getInt32Ty(Context), func2ID[iter.second]);
		args[1] = ACCUM ? ConstantInt::get(Type::getInt32Ty(Context), 1) :
			ConstantInt::get(Type::getInt32Ty(Context), 0);

		CallInst::Create(plainExecCallSiteBegin, args, "", iter.first);
	}


	insertList.clear();
	for ( auto fi = M.begin(); fi != M.end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;
				Function *callee = NULL;
				if ( CallInst *cInst = dyn_cast<CallInst>(inst) ) {
					callee = cInst->getCalledFunction();
				}
				else if ( InvokeInst *iInst = dyn_cast<InvokeInst>(inst) ) {
					callee = iInst->getCalledFunction();
				}


				if ( isa<CallInst>(inst) || isa<InvokeInst>(inst) ) {
					assert(callee != NULL);

					if ( callee->isDeclaration() ) continue;

					insertList[inst->getNextNode()] = callee;
				}
			}

	for ( auto iter : insertList ) {
		std::vector<Value *> args(0);
		args.resize(2);
		args[0] = ConstantInt::get(Type::getInt32Ty(Context), func2ID[iter.second]);
		args[1] = ACCUM ? ConstantInt::get(Type::getInt32Ty(Context), 1) :
			ConstantInt::get(Type::getInt32Ty(Context), 0);

		CallInst::Create(plainExecCallSiteEnd, args, "", iter.first);
	}


}

bool PlainExectime::runOnModule(Module& M) {
	initFuncID();
	setFunctionID(M);

	setFunctions(M);
	insertToCallSite(M);

	setIniFini(M);



	std::ofstream idfile("Function2ID.data", std::ios::out | std::ios::binary);

	idfile << "Function Name : Function ID\n";
	for ( auto iter : func2ID )
	{
		Function *func = iter.first;
		int id = iter.second;
		idfile << func->getName().str() << "\t:\t" << id << "\n";
	}
	idfile.close();

	return false;
}

