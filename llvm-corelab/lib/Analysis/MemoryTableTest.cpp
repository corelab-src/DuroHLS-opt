#include <sstream>
#include <unistd.h>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>

#include "llvm/IR/Value.h"
#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AliasAnalysis.h"

#include "corelab/Analysis/MemoryTableTest.h"
#include "corelab/Utilities/GetMemOper.h"

using namespace llvm;
using namespace corelab;

char MemoryTableTest::ID = 0;
static RegisterPass<MemoryTableTest> X("mt-test", "Memory Table Test", false, false);


void MemoryTableTest::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.addRequired<AAResultsWrapperPass>();
	AU.addRequired< LoopInfoWrapperPass >();
	AU.addRequired< PADriverTest >();
	AU.setPreservesAll();
}

bool MemoryTableTest::runOnModule(Module &M) {
	module = &M;
	PADriverTest *pa = getAnalysis< PADriverTest >().getPA();

	assert(loopInfoOf.empty() && "ERROR LOOP INFO TWICE CALLED\n\n");
	loopInfoOf.clear();
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		Function &F = *fi;
		if ( (&*fi)->isDeclaration() )
			continue;
		
		LoopInfo *li = 
			new LoopInfo(std::move(getAnalysis< LoopInfoWrapperPass >(F).getLoopInfo()));
		loopInfoOf[&*fi] = li;
	}

	paaa = new PABasedAAOPT(module, pa, loopInfoOf);

	long count = 0;
	long alias_count_paaa = 0;
	long alias_count_aa = 0;
	long alias_count_aa_must = 0;
	long aa_win = 0;
	long paaa_win = 0;
	long paaa_error= 0;

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		if ( (&*fi)->isDeclaration() ) continue;
		aa = &getAnalysis<AAResultsWrapperPass>(*fi).getAAResults();

		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;
				if ( isa<LoadInst>(inst) || isa<StoreInst>(inst) ) {
					for ( auto ii_before = (&*bi)->begin(); ii_before != ii; ii_before++ )
					{
						Instruction *inst_b = &*ii_before;
						if ( isa<LoadInst>(inst_b) || isa<StoreInst>(inst_b) ) {
							count++;

							errs() << "try\n";
							inst->dump();
							inst_b->dump();
							errs() << "\n";

							bool paaa_test = false;
							bool aa_test = false;
							bool aa_must_test = false;

							if ( !paaa->isNoAlias(getMemOper(inst), paaa->getAccessSize(inst),
										getMemOper(inst_b), paaa->getAccessSize(inst_b)) ) {
								errs() << "PAAA: Alias \n";
								alias_count_paaa++;
								paaa_test = true;
							}
							if ( !aa->isNoAlias(MemoryLocation::get(inst), MemoryLocation::get(inst_b))){
								errs() << "AA: Alias \n";
								alias_count_aa++;
								aa_test = true;
							}
							if ( aa->isMustAlias(MemoryLocation::get(inst), MemoryLocation::get(inst_b))){
								errs() << "AA Must Alias \n";
								alias_count_aa_must++;
								aa_must_test = true;
							}


							if ( !paaa_test && aa_must_test ) {
								errs() << "ERROR\n";
								paaa_error++;
							}

							if ( paaa_test && !aa_test ) {
								errs() << "AAResult win\n";
								aa_win++;
							}
							if ( !paaa_test && aa_test ) {
								errs() << "PAAA win\n";
								paaa_win++;
							}
							errs() << "\n\n";
						}
					}
				}
			}
	}

	errs() << "PAAA Alias(" << alias_count_paaa << ") / Count(" << count << ")\n";
	errs() << "AA Alias(" << alias_count_aa << ") / Count(" << count << ")\n\n";
	errs() << "MUST Alias(" << alias_count_aa_must << ") / Count(" << count << ")\n\n";

	errs() << "PAAA find but AA can not find : " << paaa_win << "\t:)\n";
	errs() << "AA find but PAAA can not find : " << aa_win << "\t:(\n";
	errs() << "Error : " << paaa_error << "\t:(\n";


	errs() << "\n\n-----------------------------Loop AA Test--------------------------------\n\n";

	paaa->initLoopAA();
	errs() << "initialize done\n\n";

	for ( auto LN : paaa->getLoopNodes() )
	{
		Function *func = (*(LN->getLoop()->block_begin()))->getParent();
		if ( func->getName() != "syndrome" ) continue;

		if ( LN->isInnerMost() ) {
			const Loop *L = LN->getLoop();
			L->dump();

			if ( LN->isSimpleForm() && LN->hasSimpleCanonical() ) {
				errs() << "It can be pipelined\n";
			}
			else
				continue;

			for ( auto bi = L->block_begin(); bi != L->block_end(); bi++ )
			for ( auto ii = (*bi)->begin(); ii != (*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;
				if ( isa<LoadInst>(inst) || isa<StoreInst>(inst) ) {
					for ( auto ii_before = (*bi)->begin(); ii_before != ii; ii_before++ )
					{
						Instruction *inst_b = &*ii_before;
						if ( isa<LoadInst>(inst_b) || isa<StoreInst>(inst_b) ) {

							pair<bool, int> dist = paaa->getDistance(LN, 
									getMemOper(inst), paaa->getAccessSize(inst),
									getMemOper(inst_b), paaa->getAccessSize(inst_b));

							inst->dump();
							inst_b->dump();
							errs() << "Loop Variant : " << dist.first << "\n";
							errs() << "Distance : " << dist.second << "\n\n";
						}
					}
				}
			}
		}
	}
	return false;
}


