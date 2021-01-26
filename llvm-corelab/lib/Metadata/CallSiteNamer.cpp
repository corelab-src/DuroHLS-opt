#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"

#include "corelab/Metadata/CallSiteNamer.h"
#include "stdio.h"

namespace corelab{
	char CallSiteNamer::ID = 0;
  static RegisterPass<CallSiteNamer> RP("callsite-namer", 
							"Generate metadata for call sites", false, false);

	static uint64_t callId = 0;
	static uint64_t getCallId(void) { return ++callId; }

	void CallSiteNamer::getAnalysisUsage(AnalysisUsage &AU) const {
		AU.setPreservesAll();
	}

	bool CallSiteNamer::runOnModule(Module &M) {
		module = &M;

		callId = 0;

		for ( auto fi = module->begin(); fi != module->end(); fi++ )
			for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
				for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
					if ( isa<CallInst>(&*ii) )
						makeMetadata(&*ii, getCallId());

		return false;
	}

	void CallSiteNamer::makeMetadata(Instruction* instruction, uint64_t Id) {
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

}
