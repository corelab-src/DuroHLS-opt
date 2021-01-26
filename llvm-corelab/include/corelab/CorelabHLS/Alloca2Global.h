#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

#include <set>
#include <list>

namespace corelab
{

using namespace llvm;
using namespace std;

class Alloca2Global: public ModulePass {
	
	public:
		static char ID;
		Alloca2Global() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "Alloca to Global"; };

		bool runOnModule(Module& M);

	private:
		Module *module;
};

}

