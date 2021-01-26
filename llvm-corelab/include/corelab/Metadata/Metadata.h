#ifndef LLVM_CORELAB_METADATA_MANAGER
#define LLVM_CORELAB_METADATA_MANAGER

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "corelab/Metadata/typedefs.h"
#include <stdint.h>

namespace corelab { 
	using namespace llvm;
	
	/// Enum and data structures
	typedef enum {
		INSTR_CALL = 0x1,
		INSTR_LOAD = 0x2,
		INSTR_STORE = 0x4,
		INSTR_OTHER = 0x8
	} INSTR_TYPE;

	typedef enum {
		CONTEXT_CALL = 0x0,
		CONTEXT_LOOP = 0x1
	} CONTEXT_TYPE;

	/* (Typedef) Context Information */
	typedef struct {
		CONTEXT_TYPE contextType;
		uint16_t callingFunctionId;
		uint16_t includedFunctionId;
		uint16_t basicBlockId;
	} ContextInfo;

	/* (Typedef) Loop Information */
	typedef struct {
		const char* name;
		uint16_t functionId;
		uint16_t basicBlockId;
	} LoopEntry;


	class Namer: public ModulePass
	{
		private:
			Module *pM;

			uint16_t functionCount;
			uint16_t callCount;
			uint16_t loopCount;
			uint16_t contextCount;
			uint16_t basicBlockCount;
			uint16_t instructionCount;
			uint16_t loadCount;
			uint16_t storeCount;

			std::map<uint16_t, uint16_t> instructionToBBId; 
			std::map<uint16_t, ContextInfo*> contextTable; // ctxId -> ctxInfo
			std::map<uint16_t, const char*> functionTable; // funcId -> name
			std::map<uint16_t, LoopEntry*> loopTable; // ctxId -> loopInfo

			void initialize();
			bool makeFunctionTable(Function &F);
			void makeMetadata(Instruction* Instruction, uint64_t id);

		public:
			static char ID;
			Namer();

			StringRef getPassName() const { return "MetadataManager"; }

			void *getAdjustedAnalysisPointer(AnalysisID PI)
			{
				return this;
			}

			void getAnalysisUsage(AnalysisUsage &AU) const;

			bool runOnLoop(Loop *L, uint16_t functionId);
			bool runOnCall(uint16_t id, uint16_t includedFunctionId, uint16_t callingFunctionId);
			bool runOnModule(Module &M);
			bool runOnFunction(Function &F);
			
			static Value* getValue(const Instruction *I);
			static uint64_t getFullId(const Instruction *I);
			static uint16_t getFuncId(const Instruction *I);
			static uint16_t getBlkId(const Instruction *I);
			static uint16_t getInstrId(const Instruction *I);
			static char getInstrType(const Instruction *I);
			void reset(Module &M);
			void runOnModuleImpl(Module &M);
	};

}
#endif
