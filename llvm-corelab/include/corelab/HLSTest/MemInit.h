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

class MemInitTest : public ModulePass {
	
	public:
		static char ID;
		MemInitTest() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "Memory Initialization Test"; };

		bool runOnModule(Module& M);

		void searchGV();
		void searchAlloca();
		pair<GlobalVariable *, AllocaInst *> findSourceGV(AllocaInst *);
		Value *findSourceValue(Value *);
		void insertMetadata();
		void genInitBinFile();

		unsigned getBitWidth(Type *);
		uint8_t getPullOutData(uint64_t, unsigned);

		bool isInitGV(GlobalVariable *gv) {
			if (gvInitSet.find(gv) == gvInitSet.end())
				return false;
			else
				return true;
		}

	private:
		Module *module;

		unsigned gvID;

		set<GlobalVariable*> gvInitSet;
		DenseMap<AllocaInst *, GlobalVariable *> alloc2GV;
		list<pair<GlobalVariable *, unsigned>> gv2ID;
		list<pair<GlobalVariable *, AllocaInst *>> erasedList;
};

}

