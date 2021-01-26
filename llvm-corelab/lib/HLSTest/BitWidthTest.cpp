#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/CallSite.h"

#include "corelab/HLSTest/BitWidthTest.h"
#include "corelab/Utilities/GetMemOper.h"

#include <iostream>
#include <fstream>
#include <iomanip>

using namespace llvm;
using namespace corelab;
using namespace std;

//const static bool debug = true;

static unsigned h_fail = 0;
static unsigned l_fail = 0;
static unsigned phi_count = 0;

char BitWidthTest::ID = 0;
static RegisterPass<BitWidthTest> X("bitwidth-test", 
		"bitwidth test", false, false);

void BitWidthTest::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.addRequired<LoopInfoWrapperPass>();
	AU.addRequired<PADriverTest>();
	AU.setPreservesAll();
}

static unsigned getRequiredBitWidth(Constant *c) {
	if ( ConstantInt *cInt = dyn_cast<ConstantInt>(c) ) {
		int64_t value = cInt->getSExtValue(); //always return 64bit data
		if ( cInt->isNegative() )
			value = value * (-1);

		unsigned i = 0;
		while ( value != 0 ){
			value = value >> 1;
			i++;
		}
		return i==0? 1 : i;
	}
	else {
		c->dump();
		assert(0);
	}
}

static unsigned max(unsigned i, unsigned k) { return i < k ? k : i; }
static unsigned min(unsigned i, unsigned k) { return i < k ? i : k; }

static unsigned getWidthType(Type *ty) {
	if ( IntegerType *iTy = dyn_cast<IntegerType>(ty) )
		return iTy->getBitWidth();
	else if ( isa<PointerType>(ty) )
		return 100;
	else if ( ty->isFloatTy() )
		return 32;
	else if ( ty->isDoubleTy() )
		return 64;
	else if ( ty->isVoidTy() )
		return 0;
	else {
		assert(0);
	}
}

unsigned BitWidthTest::getWidthValue(Value *v) {
	if ( ConstantInt *cInt = dyn_cast<ConstantInt>(v) ) {
		return getRequiredBitWidth(cInt);
	}
	else if ( Instruction *inst = dyn_cast<Instruction>(v) ) {
		if ( existWidth(inst) )
			return getWidth(inst);
		else {
			inst->dump();
			assert(0);
		}
	}
	else if ( isa<Argument>(v) ) {
		Type *ty = v->getType();
		return getWidthType(ty);
	}
	else {
		v->dump();
		assert(0);
	}
}

Instruction *BitWidthTest::checkInductionVariable(Instruction *inst, BasicBlock *bb) {
//	inst->dump();
	for ( auto ui = inst->user_begin(); ui != inst->user_end(); ++ui )
	{
		Instruction *user = dyn_cast<Instruction>(*ui);
		assert(user);

		if ( isa<PHINode>(user) )
			continue;

		if ( BranchInst *bInst = dyn_cast<BranchInst>(user) ) {
			//inst is cmpinst
			for ( unsigned i = 0; i < bInst->getNumSuccessors(); ++i )
			{
				BasicBlock *succ = bInst->getSuccessor(i);
				if ( bb == succ ) {
					assert(isa<CmpInst>(inst));
//					errs() << "find\n";
					return inst;
				}
			}
		}
		else {
			Instruction *returnInst = checkInductionVariable(user, bb);
			if ( returnInst != NULL )
				return returnInst;
		}
	}
	return NULL;
}

Instruction *BitWidthTest::getCMPInst(PHINode *phi) {
	Function *func = phi->getFunction();
	LoopInfo *li = loopInfoOf[func];
	assert(li);

	BasicBlock *phiBB = phi->getParent();
	assert(phiBB);

	Loop *L = li->getLoopFor(phiBB);
	if ( L ) {
		errs() << L->getName() << "\n";

		LoopNode_ *LN = paaa->getLoopNode(L);
		if ( LN ) {
			if ( LN->hasSimpleExitCond() ) {
				LN->getExitCondition()->dump();

				if ( LN->hasSimpleCanonical() )
					if ( LN->getInductionVariable() == phi ) {
						errs() << "FIND\n";
						return LN->getExitCondition();
					}
			}
			else
				errs() << "Do not have simpel exit cond\n";
		}
		else
			errs() << "Can not find LoopNode\n";
	}
	else
		errs() << "Can not find Loop from BB\n";


	errs() << "l_fail\n\n";
	l_fail++;
	return NULL;
}

void BitWidthTest::handlePHINode(PHINode *phi) {
	bool ff = false;
	bool itself = false;
	unsigned tester = 0;
	for ( unsigned i = 0; i < phi->getNumIncomingValues(); i++ )
	{
		Value *v = phi->getIncomingValue(i);
		if ( ConstantInt *c = dyn_cast<ConstantInt>(v) ) {
			unsigned i0 = getRequiredBitWidth(c);
			if ( tester < i0 )
				tester = i0;
		}
		else if ( Instruction *opInst = dyn_cast<Instruction>(v) ) {
			if ( v == phi )
				itself = true;
			else if ( !existWidth(opInst) )
				ff = true;
			else {
				unsigned i0 = getWidth(opInst);
				if ( tester < i0 )
					tester = i0;
			}
		}
		else
			ff = true;
	}

	if ( !ff && itself ) {
		errs() << "ITSELF!! : " << tester << "\n";
		addWidth(phi, tester);
	}
	else if ( !ff ) {
		phi_count++;
		errs() << "HERE!! : " << tester << "\n";
	}


	errs() << "\nTEST : ";
	phi->dump();
	errs() << "\n";



	//check induction
	Instruction *cmpInst = checkInductionVariable(phi, phi->getParent());
	if ( cmpInst == NULL ) {
		// this is not induction variable
		addWidthType(phi, phi->getType());

		phi->dump();
		errs() << "h_fail\n\n";
		h_fail++;

		Instruction *cmpInst2 = getCMPInst(phi);
		errs() << "\n";
	}
	else {
		// this is induction
		unsigned i0=0,i1=0;
		// comparison value const or not
		for ( unsigned i = 0; i < cmpInst->getNumOperands(); i++ )
		{
			Value *v = cmpInst->getOperand(i);
			if ( Constant *c = dyn_cast<Constant>(v) )
				i0 = getRequiredBitWidth(c);
			else if ( Instruction *opInst = dyn_cast<Instruction>(v) ) {
				if ( existWidth(opInst) )
					i0 = getWidth(opInst);
			}
		}

		// incoming value const or not
		for ( unsigned i = 0; i < phi->getNumIncomingValues(); i++ )
		{
			Value *v = phi->getIncomingValue(i);
			if ( Constant *c =  dyn_cast<Constant>(v) )
				i1 = getRequiredBitWidth(c);
			else if ( Instruction *opInst = dyn_cast<Instruction>(v) ) {
				if ( existWidth(opInst) )
					i1 = getWidth(opInst);
			}
		}

		if ( i0==0 || i1==0 )
			addWidthType(phi, phi->getType());//fail to find 
		else {
			addWidth(phi, max(i0,i1));
		}
	}
}

void BitWidthTest::searchAllInst(void) {

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;

				//alloca, call, gep, load, store, bitcast ... ( already defined )
				if ( isa<AllocaInst>(inst) ||
						isa<GetElementPtrInst>(inst) ||
						isa<BitCastInst>(inst) ) {
					addWidth(inst, 100);
				}
				else if ( CallInst *callInst = dyn_cast<CallInst>(inst) ) {
					Function *calledF = callInst->getCalledFunction();
					assert(calledF);
					Type *returnTy = calledF->getReturnType();
					addWidthType(inst, returnTy);
				}
				else if ( LoadInst *lInst = dyn_cast<LoadInst>(inst) ) {
					addWidthType(inst, lInst->getType());
				}
				else if ( isa<PtrToIntInst>(inst)) {
					addWidthType(inst, inst->getType());
				}
				else if ( isa<TruncInst>(inst) ) {
					unsigned i0 = getWidthValue(inst->getOperand(0));
					unsigned inst_width = getWidthType(inst->getType());

					addWidth(inst, min(i0,inst_width));
				}
				else if ( isa<SelectInst>(inst) ) {
					unsigned i0 = getWidthValue(inst->getOperand(1));
					unsigned i1 = getWidthValue(inst->getOperand(2));

					addWidth(inst, max(i0,i1));
				}
				else if ( isa<ZExtInst>(inst) || isa<SExtInst>(inst)) {
					addWidth(inst, getWidthValue(inst->getOperand(0)));
				}
				else if ( PHINode *phi = dyn_cast<PHINode>(inst) ) {
					handlePHINode(phi);
				}
				else { //binary
					if ( inst->getOpcode() == Instruction::Add ||
							inst->getOpcode() == Instruction::Sub ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));
						unsigned i1 = getWidthValue(inst->getOperand(1));
						unsigned max_value = max(i0,i1);

						addWidth(inst, min(max_value + 1, getWidthType(inst->getType())));
					}
					else if ( inst->getOpcode() == Instruction::Or ||
							inst->getOpcode() == Instruction::Xor ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));
						unsigned i1 = getWidthValue(inst->getOperand(1));
						unsigned max_value = max(i0,i1);

						addWidth(inst, max_value);
					}
					else if ( inst->getOpcode() == Instruction::And ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));
						unsigned i1 = getWidthValue(inst->getOperand(1));

						if ( (isa<Constant>(inst->getOperand(0)) && i0 < i1) ||
								(isa<Constant>(inst->getOperand(1)) && i0 > i1) )
							addWidth(inst, min(i0,i1));
						else
							addWidth(inst, max(i0,i1));
					}
					else if ( inst->getOpcode() == Instruction::Shl ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));

						if ( ConstantInt *cInt = dyn_cast<ConstantInt>(inst->getOperand(1)) ) {
							unsigned i1 = cInt->getZExtValue();

							addWidth(inst, min(i0+i1, getWidthType(inst->getType())));
						}
						else
							addWidthType(inst, inst->getType());
					}
					else if ( inst->getOpcode() == Instruction::Mul ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));
						unsigned i1 = getWidthValue(inst->getOperand(1));

						addWidth(inst, min(i0+i1, getWidthType(inst->getType())));
					}
					else if ( inst->getOpcode() == Instruction::LShr ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));
						
						if ( ConstantInt *cInt = dyn_cast<ConstantInt>(inst->getOperand(1)) ) {
							unsigned i1 = cInt->getZExtValue();

							addWidth(inst, i0-i1);
						}
						else
							addWidth(inst, i0);
					}
					else if ( inst->getOpcode() == Instruction::UDiv ||
							inst->getOpcode() == Instruction::URem ) {
						unsigned i0 = getWidthValue(inst->getOperand(0));

						addWidth(inst, i0);
					}
					else if ( inst->getOpcode() == Instruction::AShr ||
							inst->getOpcode() == Instruction::SDiv||
							inst->getOpcode() == Instruction::SRem ) {

						addWidthType(inst, inst->getType());
					}
					else if ( isa<ICmpInst>(inst) ) {
						addWidth(inst, 1);
					}
					else if ( isa<BranchInst>(inst) || isa<ReturnInst>(inst) || isa<StoreInst>(inst) ) {
						addWidth(inst, 0);
					}
					else {
						inst->dump();
						assert(0);
					}

				}
			}
}

void BitWidthTest::testInst() {
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		Function *func = &*fi;
		errs() << "Function : " << func->getName() << "\n\n";
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;

				if ( existWidth(inst) ) {
					inst->dump();
					errs() << " : " << getWidth(inst) << "\n\n";
				}
				else {
					inst->dump();
					assert(0);
				}
			}
	}
}

bool BitWidthTest::runOnModule(Module& M) {
	//////Initialize/////
	module = &M;

	PADriverTest *pa = getAnalysis<PADriverTest>().getPA();

	loopInfoOf.clear();
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		Function *func = &*fi;
		if ( func->isDeclaration() ) continue;

		LoopInfo *li = 
			new LoopInfo(std::move(getAnalysis<LoopInfoWrapperPass>(*func).getLoopInfo()));
		loopInfoOf[func] = li;
	}

	paaa = new PABasedAAOPT(module, pa, loopInfoOf);	

	paaa->initLoopAA();

	/////////////////////

	searchAllInst();

	errs() << "count loop fail : " << l_fail << "\n";
	errs() << "count h fail : " << h_fail << "\n\n";
	errs() << phi_count << "\n";

	testInst();

	return false;
}


