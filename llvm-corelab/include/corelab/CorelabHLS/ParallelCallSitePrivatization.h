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

class ParallelCallSitePrivatization: public ModulePass {
	
	public:
		static char ID;
		ParallelCallSitePrivatization() : ModulePass(ID) {};

		virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

		StringRef getPassName() const { return "Parallelizable CallSite Privatization"; };

		bool runOnModule(Module& M);

		bool findParallelCallSite();

		void buildCloneFunctionFromParallel(Function *, DenseMap<Function *, Function *> &);
		void changeOriginToClone(DenseMap<Function *, Function *> &, Function *, Function *);

		void setAllFunction();
		void privatizeCallInst(CallInst *);
		void apply2Function(Function *);
		void apply2AllFunction();

		unsigned getMetadata(Instruction *);
		void makeMetadata(Instruction *, uint64_t);
		void makeMetadataParallel(Instruction *);

		bool isReady(Function *func) {
			for ( auto caller : func2caller[func] )
				if ( !func2done[caller] )
					return false;
			return true;
		}

		Function *getNextFunction() {
			unsigned i = 1;
			for ( auto func : funcSet )
			{
				if ( i != seed ) {
					i++;
					continue;
				}
				if ( func2done[func] )
					continue;

				addSeed();
				return func;
			}
			for ( auto func : funcSet )
			{
				if ( func2done[func] )
					continue;

				addSeed();
				return func;
			}
			return NULL;
		}

		void addSeed() {
			unsigned size = funcSet.size();
			if ( size == seed )
				seed = 1;
			else
				seed++;
		}

	private:
		Module *module;

		set<Function *> funcSet;
		set<Function *> parallelFunctionSet;
		DenseMap<Function *, bool> func2done;
		DenseMap<Function *, set<Function *>> func2caller;

		//parallel function -> ( origin -> clone )
		DenseMap<Function *, DenseMap<Function *, Function *>> func2origin2clone;

		unsigned seed;

		set<unsigned> parallelSet;
		set<CallInst *> parallelCallSiteSet;

};

}

