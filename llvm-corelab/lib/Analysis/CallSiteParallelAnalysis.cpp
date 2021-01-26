#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "corelab/Analysis/CallSiteParallelAnalysis.h"
#include "corelab/Utilities/GetMemOper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace llvm;
using namespace corelab;

const static bool debug = true;

char CallSiteParallelAnalysis::ID = 0;
static RegisterPass<CallSiteParallelAnalysis> X("callsite-parallel-analysis", 
		"CallSiteParallelAnalysis", false, false);

cl::opt<bool> NoMF(
		"NoMF", cl::init(false), cl::NotHidden,
		cl::desc("Do not apply to corelab mem functions"));

cl::opt<bool> NoRegDep(
		"NoRegDep", cl::init(false), cl::NotHidden,
		cl::desc("Do not consider register dependence // pass this handling to llc"));


void CallSiteParallelAnalysis::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.addRequired<PADriverTest>();
	AU.setPreservesAll();
}

bool CallSiteParallelAnalysis::getUsedMemory(CallInst *callInst, set<Value *> &memories) {
	Function *func = callInst->getCalledFunction();
	assert(func);

	for ( auto bi = func->begin(); bi != func->end(); bi++ )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
		{
			Instruction *inst = &*ii;
			if ( isa<LoadInst>(inst) || isa<StoreInst>(inst) ) {
				Value *ptrV = getMemOper(inst);
				set<Value *> mSet = pa->getPointedMemory(ptrV);

				//unresolved
				if ( mSet.size() == 0 )
					return false;

				for ( auto memory : mSet )
					memories.insert(memory);
			}
			else if ( CallInst *cInst = dyn_cast<CallInst>(inst) ) {
				bool resolved = getUsedMemory(cInst, memories);
				if ( !resolved )
					return false;
			}
		}
	return true;
}

void CallSiteParallelAnalysis::collectSchedule(
														DenseMap<Instruction *, unsigned> &scheduleMap, BasicBlock *bb) {
	for ( auto ii = bb->begin(); ii != bb->end(); ii++ )
	{
		Instruction *inst = &*ii;

		for ( auto  ui = inst->user_begin(); ui != inst->user_end(); ++ui )
		{
			Instruction *user= dyn_cast<Instruction>(*ui);
			assert(user);

			if ( user->getParent() != bb || isa<PHINode>(user) )
				continue;

			for ( auto ii2 = bb->begin(); ii2 != bb->end(); ii2++ )
			{
				Instruction *instIter = &*ii2;

				if ( instIter == user ) {
					if ( isa<CallInst>(inst) || isa<LoadInst>(inst) ) {
						if ( scheduleMap[user] < scheduleMap[inst]+1 )
							scheduleMap[user] = scheduleMap[inst]+1;
					}
					else {
						if ( scheduleMap[user] < scheduleMap[inst] )
							scheduleMap[user] = scheduleMap[inst];
					}
				}
			}
		}
	}
}

void CallSiteParallelAnalysis::checkBasicBlock(BasicBlock *bb) {
	set<CallInst *> callInstSet;
	callInstSet.clear();
	DenseMap<CallInst *, set<Value *>> call2memories;
	call2memories.clear();

	for ( auto ii = bb->begin(); ii != bb->end(); ii++ )
		if ( CallInst *callInst = dyn_cast<CallInst>(&*ii) )
			callInstSet.insert(callInst);

	if ( callInstSet.size() == 0 || callInstSet.size() == 1 )
		return;

	if ( NoMF ) {
		for ( auto callIter : callInstSet )
		{
			Function *func = callIter->getCalledFunction();
			assert(func);

			std::string nameStr = func->getName().str();
			nameStr.resize(11);
			if ( nameStr == "corelab_mem" )
				return;
		}
	}

	errs() << "Try to Analyze " << bb->getName() << "\n";

	//register dependence check
	//check it(bb) has parallel call sites
	if ( !NoRegDep ) {
		DenseMap<Instruction *, unsigned> scheduleMap;
		for ( auto ii = bb->begin(); ii != bb->end(); ii++ )
			scheduleMap[&*ii] = 0;
		collectSchedule(scheduleMap, bb);

		DenseMap<unsigned, unsigned> schedule2count;
		for ( auto callInst : callInstSet )
		{
			unsigned schedule = scheduleMap[callInst];
			if ( schedule2count.count(schedule) )
				schedule2count[schedule] = schedule2count[schedule] + 1;
			else
				schedule2count[schedule] = 1;
		}

		bool hasParallelCall = false;
		for ( auto scheduleIter : schedule2count)
			if ( 1 < scheduleIter.second )
				hasParallelCall = true;

		if ( !hasParallelCall ) {
			errs() << "Has Register Dependence\n\n";
			return;
		}
	}

	//memory dependence check
	bool unresolved = false;
	for ( auto callInst : callInstSet )
	{
		set<Value *> memories;
		memories.clear();

		if ( !getUsedMemory(callInst, memories) )
			unresolved = true;
		call2memories[callInst] = memories;
	}

	if ( unresolved ) {
		errs() << "Unresolved\n\n";
		return;
	}

	set<Value *> usedMemories;
	usedMemories.clear();

	bool hasMemoryDependence = false;

	//in basic block
	for ( auto ii = bb->begin(); ii != bb->end(); ii++ )
	{
		Instruction *inst = &*ii;
		if ( isa<LoadInst>(inst) || isa<StoreInst>(inst) ) {
			Value *ptrV = getMemOper(inst);
			set<Value *> mSet = pa->getPointedMemory(ptrV);

			//unresolved
			if ( mSet.size() == 0 )
				hasMemoryDependence = true;

			for ( auto memory : mSet )
				usedMemories.insert(memory);
		}
	}

	for ( auto callInst : callInstSet )
	{
		callInst->dump();
		for ( auto memory : call2memories[callInst] )
		{
			memory->dump();
			if ( usedMemories.find(memory) != usedMemories.end() ) {
				hasMemoryDependence = true;
				errs() << "here\n";
			}
			else
				usedMemories.insert(memory);
		}
		errs() << "\n";
	}

	if ( hasMemoryDependence ) {
		errs() << "Has MemoryDependence\n\n";
		return;
	}

	errs() << "Parallel BasicBlock : " << bb->getName() << "\n";
	for ( auto callInst : callInstSet )
	{
		unsigned callId = getMetadata(callInst);
		errs() << *callInst << " : " << callId << "\n";

		parallelId.insert(callId);

		makeMetadata(callInst);
	}
	errs() << "\n\n";
}

bool CallSiteParallelAnalysis::runOnModule(Module& M) {
	module = &M;
	pa = getAnalysis<PADriverTest>().getPA();

	parallelId.clear();
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		errs() << "Function : " << (&*fi)->getName() << "\n";
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			checkBasicBlock(&*bi);
	}

	std::error_code ec_print = std::make_error_code(std::errc::io_error);
	raw_fd_ostream parallelFile("callsite_parallel.debug", ec_print, llvm::sys::fs::F_None);

	for ( auto id : parallelId )
		parallelFile << id << "\n";

	parallelFile.close();

	return true;
}

unsigned CallSiteParallelAnalysis::getMetadata(Instruction *inst) {
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

void CallSiteParallelAnalysis::makeMetadata(Instruction* instruction) {
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

