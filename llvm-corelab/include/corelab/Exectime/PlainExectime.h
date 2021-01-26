#ifndef LLVM_CORELAB_PLAIN_EXEC_H
#define LLVM_CORELAB_PLAIN_EXEC_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/DenseMap.h"

#include "corelab/Utilities/InstInsertPt.h"
#include <fstream>
#include <map>
#include <list>

namespace corelab
{
	using namespace llvm;
	using namespace std;

	class PlainExectime : public ModulePass
	{
		public:
			bool runOnModule(Module& M);

			virtual void getAnalysisUsage(AnalysisUsage &AU) const
			{
				AU.setPreservesAll();
			}

			StringRef getPassName() const { return "PlainExectime"; }

			static char ID;
			PlainExectime() : ModulePass(ID) {}

		private:

			Module *module;
			DenseMap<Function *, int> func2ID;
			DenseMap<Instruction *, Function *> insertList;

			/* Functions */

			// initialize, finalize functions
			Constant *plainExecInitialize;
			Constant *plainExecFinalize;

			// functions for context 
			Constant *plainExecCallSiteBegin;
			Constant *plainExecCallSiteEnd;

			void setFunctions(Module &M);
			void setIniFini(Module &M);
			void setFunctionID(Module &M);
			void insertToCallSite(Module &M);
	};
}

#endif

