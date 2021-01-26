#include "corelab/CorelabHLS/NetworkLibTransform.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;
using namespace corelab;

char NetworkLibTransform::ID = 0;
static RegisterPass<NetworkLibTransform> X("network-lib-transform", 
		"This Pass Is For Test", false, false);

//set all network function calls having metadata 
//whose name is "network"
bool NetworkLibTransform::runOnModule(Module& M) {
	module = &M;
	LLVMContext &Context = module->getContext();

	for (auto fi = module->begin(); fi != module->end(); fi++ )
		for (auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
		{
			bool localChange = true;

			while (localChange) {
				localChange = false;

				for (auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
				{
					Instruction *inst = &*ii;

					if ( CallInst *cInst = dyn_cast<CallInst>(inst) )
					{
						Function *fcn = cInst->getCalledFunction();
						if (fcn)
						{
							if ( fcn->getName() == "fpga_pthread_create" )
							{
								Metadata *name = (Metadata*)MDString::get(Context, "pthread create");

								Metadata *valuesArray[] = {name};
								ArrayRef<Metadata *> values(valuesArray, 1);
								MDNode *mdNode = MDNode::get(Context, values);

								inst->setMetadata("test", mdNode);

								Value *threadptr = cInst->getArgOperand(0);
								Value *fcnptr = cInst->getArgOperand(1);
								Value *arg = cInst->getArgOperand(2);

								printf("fcnPointer Dump\n");
//								fcnptr->dump();

								Value *opFirst;

								if ( User *fInst = dyn_cast<User>(fcnptr) )
								{
									if ( isa<BitCastInst>(fInst) )
										opFirst = fInst->getOperand(0);
									else
										opFirst = fInst->getOperand(0);
								}
								else
									printf("noInst\n");

								opFirst->dump();

								if (Function *f = dyn_cast<Function>(opFirst))
									printf("okokokok\n");

								localChange = true;
								break;
							}
						}
					}
				}//for end
			}
		}

	//Remove Function Definitions

	return true;
}//runOnModule end
