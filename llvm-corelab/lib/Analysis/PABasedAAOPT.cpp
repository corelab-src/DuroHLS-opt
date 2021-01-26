//Written by csKim

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/BasicBlock.h"

#include "corelab/Utilities/GetMemOper.h"
#include "corelab/Utilities/GetSize.h"
#include "corelab/Utilities/GetDataLayout.h"
#include "corelab/Analysis/PABasedAAOPT.h"

#include <vector>
#include <algorithm>
#include <math.h>

using namespace llvm;
using namespace corelab;

//const static bool debug = false;
const static bool debug = true;

static unsigned getByteSize(Type *elementTy) {
	unsigned byteSize = 0;

	if ( SequentialType *aTy = dyn_cast<SequentialType>(elementTy) )
		return getByteSize(aTy->getElementType());
	else if ( IntegerType *iTy = dyn_cast<IntegerType>(elementTy) )
		byteSize = (iTy->getBitWidth())/8;
	else if ( elementTy->isFloatTy() )
		byteSize = 4;
	else if ( elementTy->isDoubleTy() )
		byteSize = 8;
	else if ( isa<PointerType>(elementTy) )
		byteSize = 4;
	else if ( StructType *sTy = dyn_cast<StructType>(elementTy) ) {
		for ( int i = 0; i < sTy->getNumElements(); i++ )
		{
			unsigned elementBSize = getByteSize(sTy->getElementType(i));
			if ( byteSize < elementBSize )
				byteSize = elementBSize; 
		}
	}
	else {
		elementTy->dump();
		assert(0 && "Can not Handle This Element Type for Printing Byte Address Shift");
	}	
	return byteSize;
}


static unsigned getNumOfElementsFromType(Type *ty) {
	//		if ( ArrayType *aTy = dyn_cast<ArrayType>(ty) )
	if ( SequentialType *aTy = dyn_cast<SequentialType>(ty) )
	{
		unsigned numOfElements = aTy->getNumElements();
		Type *nestType = aTy->getElementType();

		unsigned nestNumOfElements = getNumOfElementsFromType(nestType);

		return numOfElements * nestNumOfElements;
	}
	else if ( StructType *sTy = dyn_cast<StructType>(ty) )
	{
		//XXX: This Struct Case is just for counting the all number of struct elements
		unsigned totalNestNumOfElements = 0;
		for ( int i = 0; i < sTy->getNumElements(); i++ )
			totalNestNumOfElements += getNumOfElementsFromType( sTy->getElementType(i) );
		return totalNestNumOfElements;
	}
	else if (  IntegerType *iTy = dyn_cast<IntegerType>(ty) )
		return 1;
	else if ( ty->isFloatTy() )
		return 1;
	else if ( ty->isDoubleTy() )
		return 1;
	else if ( isa<PointerType>(ty) )
		return 1;
	else
	{
		assert(ty);
		ty->dump();
		assert(0 &&"can not support this type of memory");
		return 0;
	}
}

static Type *getNextElementType(Type *ty, Value *operand) {
	if ( StructType *sTy = dyn_cast<StructType>(ty) ) {
		ConstantInt *offsetInt = dyn_cast<ConstantInt>(operand);
		assert(offsetInt);
		unsigned operandConstant = offsetInt->getSExtValue();
		return sTy->getElementType(operandConstant);
	}
	else if ( SequentialType *aTy = dyn_cast<SequentialType>(ty) )
		return aTy->getElementType();
	else
		return NULL;
}

static int getConstant(Instruction *inst) {
	for (int i = 0; i <inst->getNumOperands(); i++ )
		if ( ConstantInt *cInt = dyn_cast<ConstantInt>(inst->getOperand(i)) )
			return cInt->getSExtValue();

	assert(0);
	return 0;
}

static bool hasConstant(Instruction *inst) {
	for (int i = 0; i <inst->getNumOperands(); i++ )
		if ( ConstantInt *cInt = dyn_cast<ConstantInt>(inst->getOperand(i)) )
			return true;
	return false;
}

static int getNextTest(int test, Value *incV) {
	if ( Instruction *inst = dyn_cast<Instruction>(incV) ) {
		int operand = getConstant(inst);

		if ( inst->getOpcode() == Instruction::Add )
			return test + operand;
		else if ( inst->getOpcode() == Instruction::Sub )
			return test - operand;
		else if ( inst->getOpcode() == Instruction::Shl )
			return test << operand;
		else if ( inst->getOpcode() == Instruction::Mul )
			return test * operand;
		else if ( inst->getOpcode() == Instruction::Or ) {
			return (int)((uint32_t)test | (uint32_t)operand);
		}
		else if ( inst->getOpcode() == Instruction::And ) {
			return (int)((uint32_t)test & (uint32_t)operand);
		}
		else
			assert(0);
	}
	else
		assert(0);
}

/////////////////////////////Loop Node Build///////////////////////////////
bool LoopNode_::setCanonicalInductionVariableAux(const Loop *loop)
{
  BasicBlock *H = loop->getHeader();

  BasicBlock *Incoming = nullptr, *Backedge = nullptr;
  pred_iterator PI = pred_begin(H);
  assert(PI != pred_end(H) && "Loop must have at least one backedge!");
  Backedge = *PI++;
  if (PI == pred_end(H))
    return false; // dead loop
  Incoming = *PI++;
  if (PI != pred_end(H))
    return false; // multiple backedges?

  if (loop->contains(Incoming)) {
    if (loop->contains(Backedge))
      return false;
    std::swap(Incoming, Backedge);
  } else if (!loop->contains(Backedge))
    return false;

  // Loop over all of the PHI nodes, looking for a canonical indvar.
  for (BasicBlock::iterator I = H->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);

		if (Value *initValue_ = PN->getIncomingValueForBlock(Incoming))
			if (Instruction *stride_ = 
					dyn_cast<Instruction>(PN->getIncomingValueForBlock(Backedge)) )
				if ( stride_->getOperand(0) == PN ) {
					indvar = PN;
					stride = stride_;
					initValue = initValue_;
					return true;
				}
  }

	if (debug) errs() << "Can not get Induction Variable :" << loop->getName() <<"\n";
  return false;
}

bool LoopNode_::setExitCondition(const Loop *loop) {

	// Find Exit condition
	BasicBlock *exitBB = loop->getExitBlock();
	if ( exitBB ) {
		BasicBlock *singlePred = exitBB->getSinglePredecessor();
		if ( singlePred ) {
			Instruction *term = singlePred->getTerminator();
			if ( BranchInst *bInst = dyn_cast<BranchInst>(term) ) {
				if ( bInst->isConditional() ) {
					exitCondition = dyn_cast<Instruction>(bInst->getCondition());
					assert(exitCondition);
					exitBranch = bInst;
					return true;
				}
			}
		}
	}

	if (debug) errs() << "Can not get Exit Condition : " << loop->getName() << "\n";
	return false;
}

void LoopNode_::setLoopInfo() {
	bool justSimple = L->isLoopSimplifyForm();
	bool hasOneExit = L->getExitBlock() != NULL;
	simpleForm = justSimple && hasOneExit;

	simpleCanonical = setCanonicalInductionVariableAux(L);
	simpleExitCond = setExitCondition(L);
}

/////////////////////////////Loop AA Usage///////////////////////////////

pair<bool, int> PABasedAAOPT::getDistance(LoopNode_ *LN,
															Value *ptr_a, int size_a, Value *ptr_b, int size_b) {
	std::set<Value *> mSet_a = pa->getPointedMemory(ptr_a);
	std::set<Value *> mSet_b = pa->getPointedMemory(ptr_b);

	//unresolved memory
	if ( mSet_a.size() == 0 || mSet_b.size() == 0 ) {
		errs() << "unresolved pointers\n";
		if ( mSet_a.size() == 0 )
			ptr_a->dump();
		if ( mSet_b.size() == 0 )
			ptr_b->dump();
		return make_pair(false, -1);
	}

	bool same_memory_obj = false;
	for ( auto memoryA : mSet_a )
		for ( auto memoryB : mSet_b )
			if ( memoryA == memoryB )
				same_memory_obj = true;
	
	if ( same_memory_obj ) {
		//collect pointers
		list<Value *> ptrList_a;
		ptrList_a.clear();
		collectPointers(ptrList_a, ptr_a);

		list<Value *> ptrList_b;
		ptrList_b.clear();
		collectPointers(ptrList_b, ptr_b);

		Value *samePoint = getSamePoint(ptrList_a, ptrList_b);
		if ( samePoint != NULL ) {
			list<Value *> gepList_a = trimList(ptrList_a, samePoint);
			list<Value *> gepList_b = trimList(ptrList_b, samePoint);

			if ( gepList_a.size() == 0 && gepList_b.size() == 0 ) {
				if ( isa<GetElementPtrInst>(samePoint) || isa<GEPOperator>(samePoint) ) {
					list<pair<int, list<Value *>>> operList;
					operList.clear();
					collectOperations(operList, samePoint);
					if ( LN->isSimpleForm() && LN->hasSimpleCanonical() )
						if ( ((operList.front()).second).size() != 1 )
							if ( ((operList.front()).second).front() == LN->getInductionVariable() )
								return make_pair(true, 0);
				}
				return make_pair(false, -1);//always alias
			}

			//calculate offset starting from the first instruction that uses the same point.

			//collect offset calculating operations from each GEP instructions
			list<pair<int, list<Value *>>> operList_a;
			operList_a.clear();
			for ( auto gepIter : gepList_a )
			{
				//variable not in a loop -> fail
				//operlist consists of <int, (constant)> or
				// <int, (( phi, InitValue, inc, sizeofOperations, operations ) set> )
				if ( !collectOperations(operList_a, gepIter) ) {
					if ( debug ) {
						errs() << "collect operation fail\n";
						gepIter->dump();
						errs() << "\n";
					}
					return make_pair(false, -1);
				}
			}

			list<pair<int, list<Value *>>> operList_b;
			operList_b.clear();
			for ( auto gepIter : gepList_b )
				if ( !collectOperations(operList_b, gepIter) ) {
					if ( debug ) {
						errs() << "collect operation fail\n";
						gepIter->dump();
						errs() << "\n";
					}
					return make_pair(false, -1);
				}

			if ( debug ) {
				errs() << "test A\n";
				ptr_a->dump();
				for ( auto iter : operList_a )
				{
					errs() << "\tMul Size : " << iter.first << "\n";
					for ( auto iter2 : iter.second )
						iter2->dump();
					errs() << "\n";
				}
				errs() << "\n";

				errs() << "test B\n";
				ptr_b->dump();
				for ( auto iter : operList_b )
				{
					errs() << "\tMul Size : " << iter.first << "\n";
					for ( auto iter2 : iter.second )
						iter2->dump();
					errs() << "\n";
				}
				errs() << "\n";
			}

			return distanceCheck(LN, operList_a, size_a, operList_b, size_b);
		}
		else
			return make_pair(false, -1);//always alias
	}
	else
		return make_pair(false, 0);//not alias
}

pair<bool, int> PABasedAAOPT::distanceCheck(LoopNode_ *LN,
															list<pair<int, list<Value *>>> operList_a, int size_a,
															list<pair<int, list<Value *>>> operList_b, int size_b) {
	if ( LN->isSimpleForm() && LN->hasSimpleCanonical() ) {
		Value *indV = LN->getInductionVariable();
		Value *initV = LN->getInitValue();
		Instruction *incV = LN->getStride();

		if ( hasTargetIndInFirst(indV, operList_a) && 
				hasTargetIndInFirst(indV, operList_b) ) {
			list<Value *> operSet_a = (operList_a.front()).second;
			list<Value *> operSet_b = (operList_b.front()).second;

			operSet_a.pop_front(); // indV
			operSet_a.pop_front(); // initV
			operSet_a.pop_front(); // incV
			operSet_a.pop_front(); // opSize
			
			operSet_b.pop_front(); // indV
			operSet_b.pop_front(); // initV
			operSet_b.pop_front(); // incV
			operSet_b.pop_front(); // opSize

			int initInteger;
			if ( ConstantInt *initC = dyn_cast<ConstantInt>(initV) )
				initInteger = initC->getSExtValue();
			else
				initInteger = ( incV->getOpcode() == Instruction::Add ) ? 0 : 128;

			int sample_a;
			int base_a = initInteger;
			int sample_b;
			int base_b = initInteger;

			int distance_plus = 0;

			sample_a = base_a;
			for ( auto operIter : operSet_a )
				sample_a = getNextTest( sample_a, operIter );

			for ( ; distance_plus < 100; distance_plus++ )
			{
				sample_b = base_b;
				for ( auto operIter : operSet_b )
					sample_b = getNextTest( sample_b, operIter );

				if ( sample_a == sample_b )
					break;

				base_b = getNextTest( base_b, incV );
			}

			int distance_minus = 0;

			base_a = initInteger;
			sample_b = initInteger;
			for ( auto operIter : operSet_b )
				sample_b = getNextTest( sample_b, operIter );

			for ( ; distance_minus < 100; distance_minus++ )
			{
				sample_a = base_a;
				for ( auto operIter : operSet_a )
					sample_a = getNextTest( sample_a, operIter );

				if ( sample_a == sample_b )
					break;

				base_a = getNextTest( base_a, incV );
			}

			if ( distance_plus == 100 && distance_minus == 100 )
				return make_pair(false, 0);
			else if ( distance_plus == 0 && distance_minus == 0 )
				return make_pair(true, 0);
			else if ( distance_plus == 100 )
				return make_pair(true, (-1)*distance_minus);
			else if ( distance_minus == 100 )
				return make_pair(true, distance_plus);
			else
				assert(0);
		}
	}

	if ( offsetAliasCheckLoopInvariant(operList_a, size_a, operList_b, size_b) )
		return make_pair(false, -1); //always alias
	else
		return make_pair(false, 0); // always not alias
}

bool PABasedAAOPT::hasTargetIndInFirst( Value *indV, 
								list<pair<int, list<Value *>>> operList ){
	if ( operList.size() != 1 ) //TODO: think more
		return false;

	for ( auto operSet : operList )
	{
		list<Value *> constOrPhi = operSet.second;
		if ( constOrPhi.size() != 1 ) {
			Value *phiV = constOrPhi.front();
			if ( phiV == indV )
				return true;
		}
		break;
	}
	return false;
}

void PABasedAAOPT::initLoop(const Loop *L, bool innerMost) {
	LoopNode_ *LN = new LoopNode_(L, innerMost);
	loopNodeList.push_back(LN);

	LN->setLoopInfo();
	loop2node[L] = LN;
}

void PABasedAAOPT::initLoops(const Loop *L) {
	bool hasSub = L->getSubLoops().size() != 0;
	if ( !hasSub ) {
		initLoop(L, true);
	}
	else {
		initLoop(L, false);
		for ( auto subLoops : L->getSubLoops() )
			initLoops(&*subLoops);
	}
}

void PABasedAAOPT::initLoopAA(void) {
	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		Function *func = &*fi;
		if ( func->isDeclaration() ) continue;

		LoopInfo &li = *loopInfoOf[func];

		std::vector<Loop *> loops( li.begin(), li.end() );

		for (Loop *outMostIter : loops)
			initLoops(outMostIter);
	}
}

/////////////////////////////Simple AA Usage///////////////////////////////

bool PABasedAAOPT::isNoAlias(Value *ptr_a, int size_a, Value *ptr_b, int size_b) {
	std::set<Value *> mSet_a = pa->getPointedMemory(ptr_a);
	std::set<Value *> mSet_b = pa->getPointedMemory(ptr_b);

	//unresolved memory
	if ( mSet_a.size() == 0 || mSet_b.size() == 0 ) {
		errs() << "unresolved pointers\n";
		return false;
	}

	bool same_memory_obj = false;
	for ( auto memoryA : mSet_a )
		for ( auto memoryB : mSet_b )
			if ( memoryA == memoryB )
				same_memory_obj = true;
	
	if ( same_memory_obj ) {
		//collect pointers
		list<Value *> ptrList_a;
		ptrList_a.clear();
		collectPointers(ptrList_a, ptr_a);

		list<Value *> ptrList_b;
		ptrList_b.clear();
		collectPointers(ptrList_b, ptr_b);

		Value *samePoint = getSamePoint(ptrList_a, ptrList_b);
		if ( samePoint != NULL ) {
			list<Value *> gepList_a = trimList(ptrList_a, samePoint);
			list<Value *> gepList_b = trimList(ptrList_b, samePoint);

			//calculate offset starting from the first instruction that uses the same point.

			//collect offset calculating operations from each GEP instructions
			list<pair<int, list<Value *>>> operList_a;
			operList_a.clear();
			for ( auto gepIter : gepList_a )
			{
				//variable not in a loop -> fail
				//operlist consists of <int, (constant)> or
				// <int, (( phi, InitValue, inc, sizeofOperations, operations ) set> )
				if ( !collectOperations(operList_a, gepIter) ) {
					if ( debug ) {
						errs() << "collect operation fail\n";
						gepIter->dump();
						errs() << "\n";
					}
					return false;
				}
			}

			list<pair<int, list<Value *>>> operList_b;
			operList_b.clear();
			for ( auto gepIter : gepList_b )
				if ( !collectOperations(operList_b, gepIter) ) {
					if ( debug ) {
						errs() << "collect operation fail\n";
						gepIter->dump();
						errs() << "\n";
					}
					return false;
				}

			if ( debug ) {
				errs() << "test A\n";
				ptr_a->dump();
				for ( auto iter : operList_a )
				{
					errs() << "\tMul Size : " << iter.first << "\n";
					for ( auto iter2 : iter.second )
						iter2->dump();
					errs() << "\n";
				}
				errs() << "\n";

				errs() << "test B\n";
				ptr_b->dump();
				for ( auto iter : operList_b )
				{
					errs() << "\tMul Size : " << iter.first << "\n";
					for ( auto iter2 : iter.second )
						iter2->dump();
					errs() << "\n";
				}
				errs() << "\n";
			}

			//calculate reference offsets
			if ( offsetAliasCheckLoopInvariant(operList_a, size_a, operList_b, size_b) )
				return false;

			return true;
		}
		else {//can not trace
			if ( debug ) errs() << "can not find same points\n";
			return false;
		}
	}
	else
		return true;
}

//if alias -> return true
bool PABasedAAOPT::offsetAliasCheckLoopInvariant(
																					list<pair<int, list<Value *>>> operList_a, int size_a,
																					list<pair<int, list<Value *>>> operList_b, int size_b) {
	//Collect offset list of size 100
	list<int> offsetList_a;
	offsetList_a.clear();
	offsetList_a.push_back(0);
	collectOffsetList( operList_a, offsetList_a );
	
	list<int> offsetList_b;
	offsetList_b.clear();
	offsetList_b.push_back(0);
	collectOffsetList( operList_b, offsetList_b );

	if ( debug ) {
		for ( auto offset : offsetList_a )
			errs() << offset << "\n";
	}

	for ( auto offset_a : offsetList_a )
		for ( auto offset_b : offsetList_b )
		{
			// a1 <= A < a2
			// b1 <= B < b2
			int a1 = offset_a;
			int a2 = offset_a + size_a;
			int b1 = offset_b;
			int b2 = offset_b + size_b;
			
			if ( (a1 <= b1) && (b1 < a2) )
				return true;
			else if ( (b1 <= a1) && (a1 < b2) )
				return true;
		}

	return false;
}

//Collect offset list of size 100
void PABasedAAOPT::collectOffsetList(list<pair<int, list<Value *>>> operList,
																		list<int> &offsetList) {
	for ( auto operSet : operList )
	{
		int sizeOfMul = operSet.first;
		list<Value *> constOrPhi = operSet.second;
		
		if ( constOrPhi.size() == 1 ) { // == constant
			ConstantInt *constantV = dyn_cast<ConstantInt>(constOrPhi.front());
			assert(constantV);
			int constantValue = constantV->getSExtValue();
			int offset = sizeOfMul * constantValue;
			
			list<int> tmpList;
			tmpList.clear();
			for ( auto offsetIter : offsetList )
				tmpList.push_back( offsetIter + offset );

			offsetList = tmpList;
			tmpList.clear();
		}
		else { // phi
			Value *phiV = constOrPhi.front();
			constOrPhi.pop_front();

			Value *initV = constOrPhi.front();
			constOrPhi.pop_front();

			Value *incV = constOrPhi.front();
			constOrPhi.pop_front();

			Value *sizeOfOperationsV = constOrPhi.front();
			constOrPhi.pop_front();

			ConstantInt *sizeOfOperationsC = dyn_cast<ConstantInt>(sizeOfOperationsV);
			assert(sizeOfOperationsC);
			int sizeOfOperations = sizeOfOperationsC->getSExtValue();
			assert(sizeOfOperations == constOrPhi.size());

			ConstantInt *initC= dyn_cast<ConstantInt>(initV);
			assert(initC);
			int iterationTest = initC->getSExtValue();
			
			list<int> tmpList;
			tmpList.clear();

			for ( int i=0; i<100; i++ )
			{
				//if additional operations exist
				int calculatedTest = iterationTest;
				for ( auto additionalOper : constOrPhi )
					calculatedTest = getNextTest(calculatedTest, additionalOper);
				calculatedTest *= sizeOfMul;

				if ( calculatedTest < 0 ) //offset always positive
					break;

				for ( auto offsetIter : offsetList )
					tmpList.push_back( offsetIter + calculatedTest );

				iterationTest = getNextTest(iterationTest, incV);
			}
			offsetList = tmpList;
			tmpList.clear();

		}
	}
}


bool PABasedAAOPT::getInductionInc(PHINode *phiV, pair<Value *, Value *> &condition) {
	unsigned numIncoming = phiV->getNumIncomingValues();
	if ( numIncoming == 2 ) {
		bool findInc = false;
		bool findConst = false;

		for ( unsigned i = 0; i < numIncoming; i++ )
		{
			Value *comeV = phiV->getIncomingValue(i);
			if ( Instruction *comeI = dyn_cast<Instruction>(comeV) ) {
				unsigned numOperands = comeI->getNumOperands();

				if ( numOperands == 2 ) {
					bool findPhi = false;
					bool findConst_ = false;

					for ( unsigned j = 0; j < numOperands; j++ )
					{
						if ( PHINode *thePhi = dyn_cast<PHINode>(comeI->getOperand(j)) )
							if ( thePhi == phiV )
								findPhi = true;

						if ( isa<ConstantInt>(comeI->getOperand(j)) )
							findConst_ = true;
					}

					if ( findPhi && findConst_ ) {
						condition.first = comeV;
						findInc = true;
					}
					else
						return false; // inc condition fail
				}
				else
					return false; // inc instruction fail
			}
			else if ( ConstantInt *constI = dyn_cast<ConstantInt>(comeV) ) {
				condition.second = comeV;
				findConst = true;
			}
		}

		if ( findInc && findConst )
			return true;
		else
			return false;
	}
	else
		return false; // phi condition fail
}

bool PABasedAAOPT::collectOperationsAux(list<pair<int, list<Value *>>> &operList, int num,
																				list<Value *> &operations, Value *v) {
	if ( PHINode *phiInst = dyn_cast<PHINode>(v) ) {
		pair<Value *, Value *> condition;

		if ( !getInductionInc(dyn_cast<PHINode>(v), condition) ) 
			return false;

		//insert Padding
		operations.push_front(
				ConstantInt::get(Type::getInt32Ty(module->getContext()), operations.size()) );


		operations.push_front(condition.first);
		operations.push_front(condition.second);
		operations.push_front(v);
		operList.push_back( make_pair(num, operations) );
		return true;
	}
	else if ( Instruction *inst = dyn_cast<Instruction>(v) ) {
		if ( inst->getOpcode() == Instruction::Add ||
				inst->getOpcode() == Instruction::Sub ||
				inst->getOpcode() == Instruction::Shl ||
				inst->getOpcode() == Instruction::Mul ||
				inst->getOpcode() == Instruction::Or ||
				inst->getOpcode() == Instruction::And) {

			if ( !hasConstant(inst) )
				return false;

			operations.push_front(v);
			if ( collectOperationsAux( operList, num, operations, inst->getOperand(0) ) )
				return true;
			else
				return false;
		}
		else
			return false;
	}
	else
		return false;
}


bool PABasedAAOPT::collectOperations(list<pair<int, list<Value *>>> &operList, Value *v) {
	if ( isa<GetElementPtrInst>(v) || isa<GEPOperator>(v) )
	{
		User *gepUser = dyn_cast<User>(v);
		assert(gepUser);

		Type *pTy = gepUser->getOperand(0)->getType();
		Type *eTy = dyn_cast<PointerType>(pTy)->getElementType();

		int startPoint;
		if ( isa<GEPOperator>(v) ) {
			if ( gepUser->getNumOperands() == 2 )
				startPoint = 1;
			else if ( dyn_cast<GEPOperator>(v)->isInBounds() && isa<SequentialType>(eTy) )
				startPoint = 1;
			else {
				Value *zeroV = gepUser->getOperand(1);
				assert(isa<ConstantInt>(zeroV));
				assert(dyn_cast<ConstantInt>(zeroV)->getSExtValue() == 0);
				startPoint = 2;
			}
		}

		if ( isa<GetElementPtrInst>(v) ) {
			if ( gepUser->getNumOperands() == 2 )
				startPoint = 1;
			else if ( dyn_cast<GetElementPtrInst>(v)->isInBounds() && isa<SequentialType>(eTy) )
				startPoint = 1;
			else {
				Value *zeroV = gepUser->getOperand(1);
				assert(isa<ConstantInt>(zeroV));
				assert(dyn_cast<ConstantInt>(zeroV)->getSExtValue() == 0);
				startPoint = 2;
			}
		}


		for ( int i=startPoint; i< gepUser->getNumOperands(); i++ )
		{
			Value *operandV = gepUser->getOperand(i);
			assert(operandV);

			int numOfElements = getNumOfElementsFromType(eTy);
			numOfElements *= getByteSize(eTy);

			eTy = getNextElementType(eTy, operandV);

			if ( ConstantInt *cInt = dyn_cast<ConstantInt>(operandV) ) {
				list<Value *> newOperation;
				newOperation.push_back(operandV);
				operList.push_back( make_pair(numOfElements, newOperation) );
			}
			else {// arg or instruction( phi or phi + offset cal )
				list<Value *> operations;
				operations.clear();
				if ( !collectOperationsAux( operList, numOfElements, operations, operandV ) )
					return false; // can not trace operations
			}
		}
		return true;
	}
	else
		assert(0&&"value is not gep");
}

void PABasedAAOPT::collectPointers(list<Value *> &ptrList, Value *ptr) {
	if ( GetElementPtrInst *GEPInst = dyn_cast<GetElementPtrInst>(ptr) ) {
		ptrList.push_back(ptr);
		Value *ptrOperand = GEPInst->getOperand(0);
		collectPointers(ptrList, ptrOperand);
	}
	else if ( BitCastInst *bInst = dyn_cast<BitCastInst>(ptr) ) {
		ptrList.push_back(ptr);
		Value *ptrOperand = bInst->getOperand(0);
		collectPointers(ptrList, ptrOperand);
	}
	else if ( isa<GEPOperator>(ptr) || isa<BitCastOperator>(ptr) ) {
		ptrList.push_back(ptr);
		Value *ptrOperand = dyn_cast<User>(ptr)->getOperand(0);
		collectPointers(ptrList, ptrOperand);
	}
	else if ( isa<AllocaInst>(ptr) || isa<GlobalVariable>(ptr) ) { 
		// alloca, global ( end point )
		ptrList.push_back(ptr);
	}
	else if ( isa<LoadInst>(ptr) || isa<Argument>(ptr) || 
			isa<PHINode>(ptr) || isa<SelectInst>(ptr)) { 
		//memory inst //argument ( diverse points )
		ptrList.push_back(ptr);
	}
	else {
		ptr->dump();
		assert(0&&"not handle yet");
	}
}

Value *PABasedAAOPT::getSamePoint(list<Value *> list_a, list<Value *> list_b) {
	for ( auto iter_a : list_a )
		for ( auto iter_b : list_b )
			if ( iter_a == iter_b )
				return iter_a;
	return NULL;
}

list<Value *> PABasedAAOPT::trimList(list<Value *> ptrList, Value *point) {
	list<Value *> trimmed;
	trimmed.clear();

	for ( auto iter : ptrList )
	{
		if ( point == iter )
			break;

		if ( isa<GetElementPtrInst>(iter) )
			trimmed.push_front(iter);
		else if ( isa<GEPOperator>(iter) )
			trimmed.push_front(iter);

	}

	return trimmed;
}

