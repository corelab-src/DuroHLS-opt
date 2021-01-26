#ifndef LLVM_CORELAB_METADATA_MANAGER
#define LLVM_CORELAB_METADATA_MANAGER

#include "llvm/Pass.h"
#include "llvm/IR/Instruction.h"
#include <stdint.h>

namespace corelab 
{ 
	
using namespace llvm;
using namespace std;

	class CallSiteNamer: public ModulePass
	{
		public:
			static char ID;
			CallSiteNamer() : ModulePass(ID) {};

			StringRef getPassName() const { return "CallSite Namer"; }

			void getAnalysisUsage(AnalysisUsage &AU) const;

			bool runOnModule(Module &M);

			void makeMetadata(Instruction *, uint64_t);

		private:
			Module *module;
	};

}
#endif
