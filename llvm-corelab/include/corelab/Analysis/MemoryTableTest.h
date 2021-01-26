#ifndef MTTEST_H
#define MTTEST_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/LoopInfo.h"

#include "corelab/Analysis/PADriver.h"
#include "corelab/Analysis/PointerAnalysis.h"
#include "corelab/Analysis/PABasedAAOPT.h"

#include <set>
#include <vector>
#include <string>

using namespace llvm;

namespace corelab
{
	class MemoryTableTest: public ModulePass {
		public:
			static char ID;
			MemoryTableTest() : ModulePass(ID) {};

			virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

			StringRef getPassName() const { return "Inline Small Functions"; };

			bool runOnModule(Module &M);

		private:
			Module *module;
			PADriverTest *pa;
			DenseMap<const Function *, LoopInfo *> loopInfoOf;
			PABasedAAOPT *paaa;
			AAResults *aa;
	};
}

#endif
