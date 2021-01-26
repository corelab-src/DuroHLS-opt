#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

#include "llvm/Analysis/LoopInfo.h"
#include "corelab/Analysis/PABasedAAOPT.h"

#include <set>
#include <list>

namespace corelab
{

using namespace llvm;
using namespace std;

class BitWidthTest : public ModulePass {
	
	public:
		static char ID;
		BitWidthTest() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "BitWidth Test"; };

		bool runOnModule(Module& M);

		void searchAllInst();
		void testInst();

		Instruction *getCMPInst(PHINode *);
		Instruction *checkInductionVariable(Instruction *, BasicBlock *);
		void handlePHINode(PHINode *);

		void addWidth(Instruction *inst, unsigned width) { inst2width[inst] = width; }
		bool existWidth(Instruction *inst) { return inst2width.count(inst); }
		unsigned getWidth(Instruction *inst) { return inst2width[inst]; }
		unsigned getWidthValue(Value *);

		void addWidthType(Instruction *inst, Type *ty) {
			if ( IntegerType *iTy = dyn_cast<IntegerType>(ty) )
				addWidth(inst, iTy->getBitWidth());
			else if ( isa<PointerType>(ty) )
				addWidth(inst, 100);
			else if ( ty->isFloatTy() )
				addWidth(inst, 32);
			else if ( ty->isDoubleTy() )
				addWidth(inst, 64);
			else if ( ty->isVoidTy() )
				addWidth(inst, 0);
			else {
				inst->dump();
				ty->dump();
				assert(0);
			}
		}

	private:
		Module *module;
		PABasedAAOPT *paaa;
		DenseMap<const Function *, LoopInfo *> loopInfoOf;

		DenseMap<Instruction *, unsigned> inst2width;
};

}

