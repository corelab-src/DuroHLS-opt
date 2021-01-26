#ifndef LLVM_CORELAB_NETWORK_LIB_TRANSFORM_H
#define LLVM_CORELAB_NETWORK_LIB_TRANSFORM_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

namespace corelab
{
	using namespace llvm;
	using namespace std;


	class NetworkLibTransform : public ModulePass
	{
		public:
			static char ID;
			NetworkLibTransform() : ModulePass(ID) {};

			virtual void getAnalysisUsage(AnalysisUsage &AU) const
			{
				
			}

			StringRef getPassName() const { return "Network Library Transform"; };

			bool runOnModule(Module& M);

		private:
			Module *module;

	};
}

#endif
