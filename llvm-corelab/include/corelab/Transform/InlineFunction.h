#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Analysis/LoopInfo.h"

#include <set>
#include <list>

namespace corelab
{

using namespace llvm;
using namespace std;

class InlineSmallFunction : public ModulePass {
	
	public:
		static char ID;
		InlineSmallFunction() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "Inline Small Functions"; };

		bool runOnModule(Module &M);
//		bool runOnFunction(Function &F);	

		void smallFunctionSearch();
		bool checkInlinePossible(Function *);
		void makeInline();

		bool uniqueFunctionSearch(Function *);

		void updateLOC(Function *);
		bool recursiveCheck(Function *);

		bool largeStateCheck(Function *);
		unsigned getStateCount(Function *);
		bool enoughSmall(Function *);
		bool isNoInline(Function *);
		bool isOptNone(Function *);

		void setAttributeSize(Function *);

	private:
		Module *module;
		
		DenseMap<Function *, unsigned> func2loc;
		list<Function *> smallFunctions;
//		list<Function *> blackList;
		DenseMap<Function *, list<Instruction *>> func2callsite;
		
		list<Function *> uniqueFunctions;
		DenseMap<Function *, unsigned> func2callsitenum;
		DenseMap<Function *, Instruction *> func2unique;
};

}
