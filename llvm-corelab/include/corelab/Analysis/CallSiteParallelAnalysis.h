#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

#include "corelab/Analysis/PADriver.h"
#include "corelab/Analysis/PointerAnalysis.h"

#include <set>
#include <list>

namespace corelab
{

using namespace llvm;
using namespace std;

class CallSiteParallelAnalysis : public ModulePass {
	
	public:
		static char ID;
		CallSiteParallelAnalysis() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "CallSiteParallelAnalysis"; };

		bool runOnModule(Module& M);

		bool getUsedMemory(CallInst *, set<Value *> &);

		void collectSchedule(DenseMap<Instruction *, unsigned> &, BasicBlock *);
		void checkBasicBlock(BasicBlock *);

		unsigned getMetadata(Instruction *);
		void makeMetadata(Instruction *);

	private:
		Module *module;
		PADriverTest *pa;

		set<unsigned> parallelId;
};

}

