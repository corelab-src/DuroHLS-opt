
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "corelab/CorelabHLS/Alloca2Global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace llvm;
using namespace corelab;

const static bool debug = true;

char Alloca2Global::ID = 0;
static RegisterPass<Alloca2Global> X("alloca-global", 
		"Alloca to Global Variable", false, false);

void Alloca2Global::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.setPreservesAll();
}

static int id;

static int getID(void) { return ++id; }


bool Alloca2Global::runOnModule(Module& M) {
	module = &M;

	std::error_code ec_print = std::make_error_code(std::errc::io_error);
	raw_fd_ostream refFile("id2memory.info", ec_print, llvm::sys::fs::F_None);

	id = 0;

	set<Instruction *> erasedSet;
	erasedSet.clear();

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;

				if ( AllocaInst *aInst = dyn_cast<AllocaInst>(inst) ) {
//					StringRef original_name = aInst->getName();
//					StringRef tmp_name("this_is_tmp");
//					aInst->setName(tmp_name);

					refFile << getID() << "\t" << aInst->getName() << "\n";
					char str[30];
					sprintf(str, "REF_%d_REF", id);

					Type *allocaType = aInst->getType()->getElementType();

					GlobalVariable *globalV = NULL;

					aInst->dump();
					if ( allocaType->isStructTy() || allocaType->isArrayTy() || allocaType->isVectorTy() ){
						globalV = new GlobalVariable(*module, 
								aInst->getType()->getElementType(), false, GlobalValue::CommonLinkage,
								ConstantAggregateZero::get(allocaType), aInst->getName()); 
					}
					else if ( allocaType->isPointerTy() ) {
						globalV = new GlobalVariable(*module, 
								aInst->getType()->getElementType(), false, GlobalValue::CommonLinkage,
								ConstantPointerNull::get(dyn_cast<PointerType>(allocaType))); 
					}
					else {
						globalV = new GlobalVariable(*module, 
								aInst->getType()->getElementType(), false, GlobalValue::CommonLinkage,
								ConstantInt::get(allocaType, 0)); 
					}

					assert(globalV);
					aInst->replaceAllUsesWith(globalV);
					erasedSet.insert(inst);
				}
			}

	for ( auto iter : erasedSet )
		iter->eraseFromParent();

	refFile.close();

	return true;
}


