#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

#include "corelab/Analysis/PADriver.h"

#include <set>
#include <list>

namespace corelab
{

using namespace llvm;
using namespace std;

class ComplexLoadTransform : public ModulePass {
	
	public:
		static char ID;
		ComplexLoadTransform() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "Complex Load Transform Pass"; };

		bool runOnModule(Module& M);

		Value *getMemOper(Value *);
		unsigned getDataBitWidth(Type *);
		GetElementPtrInst *getGEPInst(Value *);
		Value *getConstantOperand(Value *, unsigned);

		void searchTargetLoadInstruction();
		void transformLoadInstruction();
		Value *getPtrValue(Value *, unsigned, unsigned);

	private:
		Module *module;

		list<pair<Instruction *, Value *>> targetList;
		PADriverTest *pa;

};

}

