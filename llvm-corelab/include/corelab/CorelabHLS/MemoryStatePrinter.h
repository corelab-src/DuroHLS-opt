#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

#include "corelab/Analysis/PADriver.h"

#include <set>
#include <list>
#include <vector>

namespace corelab
{

using namespace llvm;
using namespace std;

class MemoryStatePrinter : public ModulePass {
	
	public:
		static char ID;
		MemoryStatePrinter() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "Memory State Print"; };

		bool runOnModule(Module& M);

		void setFunctions(void);
		void apply2Functions(void);

		void insertStructHandler(Type *, int, Instruction *);
		void insertRuntime(int, Instruction *);

		bool haveStruct(Type *memTy) {
			if ( PointerType *pTy = dyn_cast<PointerType>(memTy) )
				return haveStruct(pTy->getElementType());
			else if ( SequentialType *sTy = dyn_cast<SequentialType>(memTy) )
				return haveStruct(sTy->getElementType());
			else if ( isa<StructType>(memTy) )
				return true;
			else
				return false;
		}

		Type *getFirstStruct(Type *ty) {
			if ( SequentialType *sTy = dyn_cast<SequentialType>(ty) )
				return getFirstStruct(sTy->getElementType());
			else if ( isa<StructType>(ty) )
				return ty;
			else
				assert(0 &&"should be struct");
		}

		//Runtime Functions
		Constant *printBits;
		Constant *printEnd;
		Constant *registerStruct;
		Constant *startStruct;
		Constant *addElement;

	private:
		Module *module;

		PADriverTest *pa;

		DenseMap<Value *, Constant *> mem2name;
		DenseMap<Value *, int > mem2id;
};

}

