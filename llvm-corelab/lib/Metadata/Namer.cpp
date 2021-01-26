/***
 * Namer.cpp
 *
 * Generate Metadatas for load & store instructions, loop, calls
 * etc..., informations will be stored on both outer metadata.
 * "llvm/corelab/LoadNamer.h" will help to load the
 * metadata to client.
 *
 * */

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Debug.h"

#include "corelab/Metadata/Metadata.h"
#include "stdio.h"

/// Put together function id, basicblock id, instruction id, instruction type
/// on an unsinged integer. This will be stored in metadata.
#define mergeID(funcId, bbId, instrId, instrType) \
	(((uint64_t) (funcId & 0xFFFF) << 48) \
	 | ((uint64_t) (bbId & 0xFFFF) << 32) \
	 | ((uint64_t) (instrId & 0xFFFF) << 16) \
	 | ((uint64_t) (instrType & 0xF)))
#define GET_INST_ID(I) ((I >> 16 ) & 0xFFFF )
#define GET_BLK_ID(I) (( I >> 32 ) & 0xFFFF )
#define GET_FUNC_ID(I) (( I >> 48 ) & 0xFFFF )
#define GET_TYPE(I) (I & 0xF)

namespace corelab{
	char Namer::ID = 0;
	namespace {
          static RegisterPass<Namer> RP("metadata-namer", 
							"Generate the metadata of the program", false, false);
	}

	Namer::Namer() : ModulePass(ID) {}

	void Namer::getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<LoopInfoWrapperPass>();
		AU.setPreservesAll();
	}

	void Namer::initialize() {
		functionCount = 0;
		callCount = 0;
		loopCount = 0;
		contextCount = 0;
		basicBlockCount = 0;
		instructionCount = 0;
		loadCount = 0;
		storeCount = 0;
	
		instructionToBBId.clear();
		contextTable.clear();
		functionTable.clear();
		loopTable.clear();
	}

	bool Namer::runOnLoop(Loop *L, uint16_t functionId) {
		++contextCount;
		++loopCount;
		BasicBlock *header = L->getHeader();

		// make the context entry 
		ContextInfo *contextInfo =
			(ContextInfo*)malloc(sizeof(ContextInfo));
		contextInfo->contextType = CONTEXT_LOOP;
		contextInfo->includedFunctionId = functionId;
		//contextInfo->basicBlockId = Namer::getBlkId(&(header->front())); 
		contextTable[contextCount] = contextInfo;

		// save the loop id to identify it using header name.
		LoopEntry *loopEntry = (LoopEntry*)malloc(sizeof(LoopEntry));
		loopEntry->name = header->getName().data();
		loopEntry->functionId = functionId;
		//loopEntry->basicBlockId = Namer::getBlkId(&(header->front())); 
		loopTable[contextCount] = loopEntry;

		// for each subloops, call runOnLoop function recursively. 
		std::list<Loop*> subloops(L->getSubLoops().begin(),
				L->getSubLoops().end());
		while (!subloops.empty()) {
			Loop *loop = subloops.front();
			subloops.pop_front();
			runOnLoop(loop, functionId);
		}
		
		return false;
	}

	bool Namer::runOnCall(uint16_t bbid, uint16_t includedFunctionId, uint16_t callingFunctionId) {
		++contextCount;
		++callCount;

		// make the context entry
		ContextInfo *contextInfo =
			(ContextInfo*)malloc(sizeof(ContextInfo));
		contextInfo->contextType = CONTEXT_CALL;
		contextInfo->callingFunctionId = callingFunctionId;
		contextInfo->includedFunctionId = includedFunctionId;
		contextInfo->basicBlockId = bbid;
		contextTable[contextCount] = contextInfo;

		return false;
	}

	bool Namer::runOnModule(Module &M) {
		runOnModuleImpl(M);
		return false;
	}

	void Namer::runOnModuleImpl(Module &M)
	{
		pM = &M;
		initialize();

		for (Module::iterator fi = M.begin(), fe = M.end(); fi != fe; ++fi) {
			Function *f = &*fi;
			if (f->isDeclaration ())
				continue;
			makeFunctionTable(*f);
		}

		for (Module::iterator fi = M.begin(), fe = M.end(); fi != fe; ++fi) {
			Function *f = &*fi;
			if (f->isDeclaration ())
				continue;
			runOnFunction(*f);
		}

		// Print out all the metadata information
		FILE *output = fopen("metadata.profile", "w");
		fprintf (output, "#REGION metadata\n");
		fprintf (output, ":LOAD_COUNT %d\n", loadCount);
		fprintf (output, ":STORE_COUNT %d\n", storeCount);
		fprintf (output, ":BASICBLOCK_COUNT %d\n", basicBlockCount);
		fprintf (output, ":FUNCTION_COUNT %d\n", functionCount);
		fprintf (output, ":LOOP_COUNT %d\n", loopCount);
		fprintf (output, ":CALL_COUNT %d\n", callCount);
		fprintf (output, ":CONTEXT_COUNT %d\n", contextCount);

		fprintf (output, ":FUNCTION_TABLE\n");
		for(std::map<uint16_t, const char*>::iterator fi = functionTable.begin(), 
				fe = functionTable.end(); fi != fe; ++fi)
			fprintf (output, "[%04d] %s\n", fi->first, fi->second);

		fprintf (output, ":LOOP_TABLE\n");
		for(std::map<uint16_t, LoopEntry*>::iterator li = loopTable.begin(), 
				le = loopTable.end(); li != le; ++li)
			fprintf (output, "[%04d] %s in %d, %d\n", li->first,
					li->second->name, li->second->functionId,
					li->second->basicBlockId);

		fprintf (output, ":CONTEXT_TABLE\n");
		for(std::map<uint16_t, ContextInfo*>::iterator ci = contextTable.begin(),
				ce = contextTable.end(); ci != ce; ++ci)
			if (ci->second->contextType == CONTEXT_CALL)
				fprintf (output, "[%04d] call %d in %d, %d\n", ci->first,
						ci->second->callingFunctionId,
						ci->second->includedFunctionId,
						ci->second->basicBlockId);
			else if (ci->second->contextType == CONTEXT_LOOP)
				fprintf (output, "[%04d] loop in %d, %d\n", ci->first,
						ci->second->includedFunctionId,
						ci->second->basicBlockId);

		fprintf (output, ":INSTR_INFO\n");
		for(std::map<uint16_t, uint16_t>::iterator ii = instructionToBBId.begin(),
				ie = instructionToBBId.end(); ii != ie; ++ii)
			fprintf (output, "[%04d] in %d\n", ii->first, ii->second);
		
		fclose(output);

		return;
	}

	bool Namer::makeFunctionTable(Function &F)
	{
		++functionCount;

		// add the function Information to functionTable.
		functionTable[functionCount] = F.getName().data();
		return false;
	}

	void Namer::makeMetadata(Instruction* instruction, uint64_t Id) {
		LLVMContext &context = instruction->getModule()->getContext();
		//XXX: Is it okay to cast Value* to Metadata* directly?
		Constant* IdV = ConstantInt::get(Type::getInt64Ty(context), Id);
		Metadata* IdM = (Metadata*)ConstantAsMetadata::get(IdV);
		Metadata* valuesArray[] = {IdM};
		ArrayRef<Metadata *> values(valuesArray, 1);
		MDNode* mdNode = MDNode::get(context, values);
		NamedMDNode *namedMDNode = pM->getOrInsertNamedMetadata("corelab.namer");
		namedMDNode->addOperand(mdNode);
		instruction->setMetadata("namer", mdNode);
		return;
	}

	bool Namer::runOnFunction(Function &F) {
		pM = F.getParent();

		// find the function id formt the function table
		uint16_t functionId = 0;
		for(std::map<uint16_t, const char*>::iterator fi = functionTable.begin(), 
				fe = functionTable.end(); fi != fe; ++fi)
		{
			StringRef target(fi->second);
			if (F.getName().equals(target))
				functionId = fi->first;
		}

		// skim the all instructions to count load, stores. 
		for (Function::iterator bi = F.begin(), be = F.end(); bi != be; ++bi) {
			++basicBlockCount;
			for (BasicBlock::iterator ii = bi->begin(), ie = bi->end(); ii != ie; ++ii) {
				Instruction *instruction = &*ii;
				++instructionCount;

				// case load. loadId is even number;
				if(instruction->getOpcode() == Instruction::Load) {
					++loadCount;
					uint16_t loadId = loadCount << 1 ;
					instructionToBBId[loadId] = basicBlockCount; 
					
					uint64_t Id = mergeID(functionId, basicBlockCount,
							loadId, INSTR_LOAD);
					makeMetadata(instruction, Id);
				}
				// case store. storeId is odd number;
				else if(instruction->getOpcode() == Instruction::Store) {
					++storeCount;
					uint16_t storeId = (storeCount << 1) | 0x1;
					instructionToBBId[storeId] = basicBlockCount;

					uint64_t Id = mergeID(functionId, basicBlockCount, 
							storeId, INSTR_STORE);
					makeMetadata(instruction, Id);
				}
				// case call. Checking called function is internal or
				// external. If it is internal, mark the metadata with call
				// id. Or external, mark it with overall instruction id.
				else if(instruction->getOpcode() == Instruction::Call) {
					CallInst *ci = (CallInst*)instruction;
					Function *callingFunction = ci->getCalledFunction();
					uint16_t callingFunctionId = 0;
					bool counted = false;
					if (callingFunction != NULL) {
						for(std::map<uint16_t, const char*>::iterator fi = functionTable.begin(), 
								fe = functionTable.end(); fi != fe; ++fi) {
							StringRef target(fi->second);
							if (callingFunction->getName().equals(target)) {
								callingFunctionId = fi->first;
								runOnCall(basicBlockCount, functionId, callingFunctionId);
								counted = true;
							}
						}
					}

					uint16_t callId;
					if (counted) {
						callId = contextCount; 
						uint64_t Id = mergeID(functionId, basicBlockCount, callId, INSTR_CALL);
						makeMetadata(instruction, Id);
					}
					else {
					uint64_t Id = mergeID(functionId, basicBlockCount,
							instructionCount, INSTR_OTHER);
					makeMetadata(instruction, Id);
					}
				}
				else { 
					uint64_t Id = mergeID(functionId, basicBlockCount,
							instructionCount, INSTR_OTHER);
					makeMetadata(instruction, Id);
				}
			}
		}

		// skim the all the loops, collect the loop context information.
		LoopInfo &li = getAnalysis< LoopInfoWrapperPass >(F).getLoopInfo();
		std::list<Loop*> loops(li.begin(), li.end());

		while (!loops.empty()) {
			Loop *loop = loops.front();
			loops.pop_front();
			runOnLoop(loop, functionId);
		}

		return false;
	}

	//FIXED BY juhyun
	Value* Namer::getValue(const Instruction *I) {
		LLVMContext &context = I->getModule()->getContext();
		MDNode* md = I->getMetadata("namer");
		//if(md==NULL) return NULL;
		//assert(md && "ERROR:: I->getMetadata(\"namer\") returns Null");
		Metadata* m = md->getOperand(0).get();
		//assert(m && "ERROR:: md->getOperand(0).get(); returns Null");
		ValueAsMetadata *VMD = dyn_cast<ValueAsMetadata>(m);
		assert(VMD && "ERROR::VMD is NULL");
		Value *v = VMD->getValue();
		assert(v && "ERROR::VMD->getValue() returns NULL");
		//MetadataAsValue* v = MetadataAsValue::getIfExists(context, m);
		return v;
	}

	uint64_t Namer::getFullId(const Instruction *I) {
		Value* v = getValue(I);
		//return 0; // who did this?
		//if(v==NULL) return -1; // i wanna be sure
		assert(v && "ERROR: getValue(I) is null!!");
		ConstantInt* cv = dyn_cast<ConstantInt>(v);
		if(cv == NULL){
			errs()<<"###~~##:"<<*(v->getType())<<"\n";
		}

		assert(cv && "ERROR: converting metadata to ConstantInt fails!!");
		uint64_t metaData = cv->getSExtValue();
		return metaData;
	}

	uint16_t Namer::getFuncId(const Instruction *I) {
		return GET_FUNC_ID(getFullId(I));
	}

	uint16_t Namer::getBlkId(const Instruction *I) {
		return GET_BLK_ID(getFullId(I));
	}

	uint16_t Namer::getInstrId(const Instruction *I) {
		return GET_INST_ID(getFullId(I));
	}

	char Namer::getInstrType(const Instruction *I) {
		return GET_TYPE(getFullId(I));
	}

	void Namer::reset(Module& M) {
		runOnModuleImpl(M);
		return;
	}
}
