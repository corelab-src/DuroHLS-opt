#include "corelab/CorelabHLS/ComplexLoadTransform.h"

using namespace llvm;
using namespace std;
using namespace corelab;

char ComplexLoadTransform::ID = 0;
static RegisterPass<ComplexLoadTransform> X("loadTransform", 
		"complex load transformation", false, false);

void ComplexLoadTransform::getAnalysisUsage( AnalysisUsage &AU ) const {
	AU.addRequired< PADriverTest>();
}

static bool hasStruct(Type *ty) {
	if ( ArrayType *aTy = dyn_cast<ArrayType>(ty) )
		return hasStruct(aTy->getElementType());
	else if ( StructType *sTy = dyn_cast<StructType>(ty) ) 
		return true;
	else
		return false;
}

static Type *getNestedType(Type *ty) {
	if ( PointerType *pTy = dyn_cast<PointerType>(ty) )
		return getNestedType(pTy->getElementType());
	else if ( ArrayType *aTy = dyn_cast<ArrayType>(ty) )
		return getNestedType(aTy->getElementType());
	else if ( StructType *sTy = dyn_cast<StructType>(ty) ) {
		Type *mostNested = NULL;
		for ( unsigned i = 0; i < sTy->getNumElements(); i++ )
		{
			Type *elementTy = sTy->getElementType(i);
			if ( mostNested == NULL )
				mostNested = getNestedType(elementTy);
			else {
				if ( mostNested != getNestedType(elementTy) )
					assert(0);
			}
		}
		assert(mostNested != NULL);
		return mostNested;
	}
	else
		return ty;
}

unsigned ComplexLoadTransform::getDataBitWidth(Type *ty) {
	if ( ArrayType *aTy = dyn_cast<ArrayType>(ty) )
		return getDataBitWidth(aTy->getElementType());
	else if ( StructType *sTy = dyn_cast<StructType>(ty) ) {
		unsigned maxBitWidth = 0;
		for ( unsigned i = 0; i < sTy->getNumElements(); ++i ) {
			unsigned bitWidth = getDataBitWidth(sTy->getElementType(i));
			if ( maxBitWidth < bitWidth )
				maxBitWidth = bitWidth;
		}
		return maxBitWidth;
//		assert(0 &&"Can not Handle this struct type on load transformation");
	}
	else if ( IntegerType *intType = dyn_cast<IntegerType>(ty) )
		return intType->getBitWidth();
	else if ( ty->isFloatTy() )
		return 32;
	else if ( ty->isDoubleTy() )
		return 64;
	else if ( PointerType *pTy = dyn_cast<PointerType>(ty) )
		return 16;
	else {
		ty->dump();
		assert(0 &&"TODO : Handle This Type");
	}
}

Value *ComplexLoadTransform::getMemOper(Value *v) {
	if ( LoadInst *lInst = dyn_cast<LoadInst>(v) )
		return lInst->getPointerOperand();
	else if ( StoreInst *sInst = dyn_cast<StoreInst>(v) )
		return sInst->getPointerOperand();
	else
		return NULL;
}

GetElementPtrInst *ComplexLoadTransform::getGEPInst(Value *inst) {
	if ( Value *pointerV = getMemOper(inst) ) {
		if ( isa<BitCastOperator>(pointerV) )
			return getGEPInst(dyn_cast<User>(pointerV)->getOperand(0));
		else if ( Instruction *operandInst = dyn_cast<Instruction>(pointerV) )
			return getGEPInst(operandInst);
		else {
			inst->dump();
			pointerV->dump();
			assert(0 && "Can not Find GEP from Load Instruction");
		}
	}
	else if ( isa<BitCastInst>(inst) )
		return getGEPInst(dyn_cast<Instruction>(inst)->getOperand(0));
	else if ( isa<GetElementPtrInst>(inst) )
		return dyn_cast<GetElementPtrInst>(inst);
	else
		return NULL;
}

Value *ComplexLoadTransform::getConstantOperand(Value *inst, unsigned offset) {
	Type *ty = inst->getType();
	LLVMContext &Context = module->getContext();
	if ( IntegerType *iTy = dyn_cast<IntegerType>(ty) ) {
		unsigned bitWidth = iTy->getBitWidth();
		if ( bitWidth == 8 )
			return ConstantInt::get(Type::getInt8Ty(Context), offset);
		else if ( bitWidth == 16 )
			return ConstantInt::get(Type::getInt16Ty(Context), offset);
		else if ( bitWidth == 32 )
			return ConstantInt::get(Type::getInt32Ty(Context), offset);
		else if ( bitWidth == 64 )
			return ConstantInt::get(Type::getInt64Ty(Context), offset);
		else {
			errs() << bitWidth << "\n";
			assert(0);
		}
	}
	else
		return NULL;
}

void ComplexLoadTransform::searchTargetLoadInstruction() {
	for ( auto fi = module->begin(); fi != module->end(); ++fi )
	{
		Function *func = &*fi;
		if ( func->isDeclaration() ) continue;

//		std::string nameStr = func->getName().str();
//		nameStr.resize(11);
//		if ( nameStr == "corelab_mem" )
//			continue;

		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); ++bi )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ++ii )
			{
				Instruction *inst = &*ii;
				if ( isa<LoadInst>(inst) || isa<StoreInst>(inst) ) {
					Value *pointerV = getMemOper(inst);
					assert(pointerV);
					Type *loadTy;
					if ( isa<LoadInst>(inst) )
						loadTy = inst->getType();
					else
						loadTy = inst->getOperand(0)->getType();
					assert(loadTy);
					if ( IntegerType *intTy = dyn_cast<IntegerType>(loadTy) ) {
						unsigned bitWidth = intTy->getBitWidth(); // load type

						set<Value *> pointedMemory = pa->getPointedMemory(pointerV);
						if ( pointedMemory.size() == 1 ) {
							Value *memoryV = *pointedMemory.begin();
							PointerType *pTy = dyn_cast<PointerType>(memoryV->getType());
							assert(pTy);
							unsigned memoryDataWidth = getDataBitWidth(pTy->getElementType());
							
							if ( memoryDataWidth < bitWidth ) {
								inst->dump();
								memoryV->dump();
								errs() << bitWidth << " : " << memoryDataWidth << "\n";
								targetList.push_back( make_pair(inst, memoryV) );
							}
						}
						else if ( pointedMemory.size() != 0 ) {
							unsigned previousWidth = 0; // memory object type
							for ( auto memoryV : pointedMemory )
							{
								PointerType *pTy = dyn_cast<PointerType>(memoryV->getType());
								assert(pTy);

								if ( hasStruct(pTy->getElementType()) ) {
									previousWidth = bitWidth;
									break;
								}

								if ( previousWidth == 0 ) {
									previousWidth = getDataBitWidth(pTy->getElementType());
								}
								else if ( previousWidth != getDataBitWidth(pTy->getElementType()) ) {
									inst->dump();
									memoryV->dump();
									assert(0 && "Load something from various types of memories");
								}
							}

							if ( previousWidth < bitWidth ) {
								inst->dump();
								errs() << bitWidth << " : " << previousWidth << "\n";

								targetList.push_back( make_pair(inst, *pointedMemory.begin()) );
							}
						}
					}//integer type
				}//load inst
			}
	}
}

Value *ComplexLoadTransform::getPtrValue(Value *ptr, unsigned instWidth, unsigned memWidth) {
	LLVMContext &Context = module->getContext();
	unsigned shiftUnsigned = 0;
	if ( instWidth / memWidth == 2)
		shiftUnsigned = 1;
	else if ( instWidth / memWidth == 4)
		shiftUnsigned = 2;
	else if ( instWidth / memWidth == 8)
		shiftUnsigned = 3;
	//TODO: Add More Cases
	//XXX: Hard Code
	if ( GetElementPtrInst *gepInst = dyn_cast<GetElementPtrInst>(ptr) ) {
		BitCastInst *bInst = dyn_cast<BitCastInst>(gepInst->getOperand(0));

		Value *operand;
		if ( bInst ) {
			operand = bInst->getOperand(0);
		}
		else if ( BitCastOperator *bOperator = dyn_cast<BitCastOperator>(gepInst->getOperand(0)) ) {
			operand = bOperator->getOperand(0);
		}
		else {
			gepInst->dump();
			assert(0);
		}

//		Value *operand = bInst->getOperand(0);
		PointerType *pTy = dyn_cast<PointerType>(operand->getType());
		operand->dump();
		assert(pTy);
		assert( getDataBitWidth(pTy->getElementType()) == memWidth );

		//bitcast ptr operand + only one offset
		assert(gepInst->getNumOperands() == 2);

		Value *offset = gepInst->getOperand(1);
		Value *shiftOperand = getConstantOperand(offset, shiftUnsigned);
		Instruction *newShiftedInst = (Instruction *)
			BinaryOperator::Create(Instruction::Shl, offset, shiftOperand,"offset_shift",gepInst);

		std::vector<Value *> actuals(0);
		actuals.resize(1);
		actuals[0] = newShiftedInst;

		PointerType *peeTy = PointerType::get(getNestedType(pTy), 
			pTy->getPointerAddressSpace());

		if ( isa<GlobalVariable>(operand) ) {
			BitCastInst *newBInst = new BitCastInst(operand, peeTy, "", gepInst);
			operand = newBInst;
		}

		GetElementPtrInst *newGEP = 
			GetElementPtrInst::Create(peeTy->getElementType(), 
					operand, actuals, "offset_shift_gep", gepInst);

		return newGEP;
	}
	else if ( BitCastInst *bInst = dyn_cast<BitCastInst>(ptr) ) {
		Value *operand = bInst->getOperand(0);
		PointerType *pTy = dyn_cast<PointerType>(operand->getType());
		operand->dump();
		assert(pTy);
		assert( getDataBitWidth(pTy->getElementType()) == memWidth );

		return operand;
	}
	else if ( PHINode *phiNode = dyn_cast<PHINode>(ptr) ) {
		Value *gepV = phiNode->getIncomingValue(0);
		GetElementPtrInst *gepInst = dyn_cast<GetElementPtrInst>(gepV);
		BasicBlock *gepBB = phiNode->getIncomingBlock(0);

		Value *bitcastV = phiNode->getIncomingValue(1);
		BitCastInst *bInst = dyn_cast<BitCastInst>(bitcastV);
		BasicBlock *bitcastBB = phiNode->getIncomingBlock(1);

		assert(gepInst);
		assert(bInst);

		//PHI insert
		PHINode *newPHI = PHINode::Create(bInst->getOperand(0)->getType(), 2, "", phiNode);

		//Bitcast Insert
		Value *operand = bInst->getOperand(0);
		PointerType *pTy = dyn_cast<PointerType>(operand->getType());
		operand->dump();
		assert(pTy);
		assert( getDataBitWidth(pTy->getElementType()) == memWidth );

		PointerType *peeTy = PointerType::get(getNestedType(pTy), 
			pTy->getPointerAddressSpace());

		BitCastInst *newBInst = new BitCastInst(bInst, peeTy, "", bitcastBB->getTerminator());

		//GEP insert
		ConstantInt *cInt = dyn_cast<ConstantInt>(gepInst->getOperand(1));
		assert(cInt);
		unsigned offset = cInt->getZExtValue();

		std::vector<Value *> actuals(0);
		actuals.resize(1);
		actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), (instWidth / memWidth) * offset);

		//Change Phi node inputs
		GetElementPtrInst *newGEP = 
			GetElementPtrInst::Create(peeTy->getElementType(),
					newPHI, actuals, "", gepInst);

		newPHI->addIncoming(newGEP, newGEP->getParent());
		newPHI->addIncoming(newBInst, newBInst->getParent());

		return newPHI;
	}
	else {
		ptr->dump();
		assert(0);
	}
}


void ComplexLoadTransform::transformLoadInstruction() {
	LLVMContext &Context = module->getContext();
	for ( auto target : targetList ) 
	{
		Instruction *inst = target.first;
		unsigned instWidth;
		if ( isa<LoadInst>(inst) )
			instWidth = getDataBitWidth(inst->getType());
		else
			instWidth = getDataBitWidth(inst->getOperand(0)->getType());

		Value *mem = target.second;
		PointerType *pTy = dyn_cast<PointerType>(mem->getType());
		assert(pTy);

		unsigned memWidth = getDataBitWidth(pTy->getElementType());
		errs() << "Try to Transform :\n";
		errs() << *inst << " : " << instWidth << "\n";
		errs() << *mem << " : " << memWidth << "\n\n";

		if ( isa<LoadInst>(inst) ) {
			DenseMap<unsigned, Instruction *> newLoadMap;
			newLoadMap.clear();

//			Value *ptrValue = inst->getOperand(0);
			Value *ptrValue = getPtrValue(inst->getOperand(0), instWidth, memWidth);

			unsigned additional = ( instWidth / 8 ) - ( memWidth / 8 );
			for ( unsigned i = 0; i <= additional; ++i )
			{
				Type *memType = pTy->getElementType();
				std::vector<Value *> actuals(0);

				PointerType *peeTy = PointerType::get(getNestedType(pTy), 
						pTy->getPointerAddressSpace());
//				BitCastInst *bInst = new BitCastInst(ptrValue, peeTy, "", inst);
//				bInst->dump();

				actuals.resize(1);
				actuals[0] = ConstantInt::get(Type::getInt64Ty(Context), i);

				peeTy->dump();
				ptrValue->dump();
				GetElementPtrInst *newGEP = 
					GetElementPtrInst::Create(peeTy->getElementType(), 
							ptrValue, actuals, "by_loadT_gep", inst);
//							bInst, actuals, "by_loadT_gep", inst);
				newGEP->dump();

				LoadInst *newLoad = new LoadInst(newGEP, "by_loadT_l", inst);
				newLoad->dump();

				newLoadMap[i] = newLoad;
			}

			list<Instruction *> newShiftList;
			newShiftList.clear();
			for ( auto newLoadIter : newLoadMap )
			{
				unsigned loadOffset = newLoadIter.first;
				Instruction *loadInst = newLoadIter.second;
				unsigned shiftOffset = loadOffset * 8;

				errs() << loadOffset << " : " << *loadInst << "\n";

				ZExtInst *newCast = 
					new ZExtInst(loadInst, inst->getType(), "by_loadT_cast", inst);
				Value *shiftOperand = getConstantOperand(inst, shiftOffset);
				Instruction *newShiftedInst = (Instruction *)
					BinaryOperator::Create(Instruction::Shl, newCast, shiftOperand,"by_loadT_shift",inst);

				newShiftList.push_back(newShiftedInst);
			}

			errs() << "\n";

			Instruction *previousInst = NULL;
			for ( auto newShiftIter : newShiftList )
			{
				Instruction *shiftInst = newShiftIter;

				errs() << "Shift Inst : " << *shiftInst << "\n";

				if ( previousInst == NULL )
					previousInst = shiftInst;
				else {
					Instruction *combineInst = (Instruction *)
						BinaryOperator::Create(Instruction::Or,previousInst,shiftInst,"by_loadT_comb",inst);
					previousInst = combineInst;
				}
			}

			errs() << "\n";

			Instruction *finalInst = previousInst;
			assert(finalInst);

			errs() << "Fianl Inst : " << *finalInst << "\n\n";

			inst->replaceAllUsesWith(finalInst);
			if ( inst->isSafeToRemove() )
				inst->eraseFromParent();
		}
		else if ( isa<StoreInst>(inst) ) {
			Value *storedV = inst->getOperand(0);
			assert(storedV);

//			Value *ptrValue = inst->getOperand(1);
			Value *ptrValue = getPtrValue(inst->getOperand(1), instWidth, memWidth);

			unsigned additional = ( instWidth / 8 ) - ( memWidth / 8 );

			for ( unsigned i = 0; i <= additional; ++i )
			{
				unsigned shiftOffset = i * 8;
				Value *shiftOperand = getConstantOperand(storedV, shiftOffset);
				Instruction *newShiftInst = (Instruction *)
					BinaryOperator::Create(Instruction::LShr,storedV,shiftOperand,"by_storeT_shift",inst);
					
				TruncInst *newTrunc =
					new TruncInst(newShiftInst, getNestedType(pTy), "by_storeT_trunc", inst);

				PointerType *peeTy = PointerType::get(getNestedType(pTy),
						pTy->getPointerAddressSpace());
//				BitCastInst *bInst = new BitCastInst(ptrValue, peeTy, "", inst);

				std::vector<Value *> actuals(0);
				actuals.resize(1);
				actuals[0] = ConstantInt::get(Type::getInt64Ty(Context), i);

				GetElementPtrInst *newGEP = 
					GetElementPtrInst::Create(peeTy->getElementType(), 
							ptrValue, actuals, "by_storeT_gep", inst);
//							bInst, actuals, "by_storeT_gep", inst);
				newGEP->dump();

				StoreInst *newStore = new StoreInst(newTrunc, newGEP, "by_storeT_s", inst);
			}

			if ( inst->isSafeToRemove() )
				inst->eraseFromParent();
		}
	}
}

bool ComplexLoadTransform::runOnModule(Module &M) {
	module = &M;
	pa = getAnalysis<PADriverTest>().getPA();

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
	{
		Function *func = &*fi;

		string func_name = func->getName().str();
		unsigned i = 0;
		for ( auto ch : func_name )
		{
			if ( (ch) == '.' || (ch) == '-' )
				func_name.replace(i,1,"_");
			i++;
		}
		StringRef new_name(func_name);

		func->setName(new_name);
	}

	searchTargetLoadInstruction();

	transformLoadInstruction();

	if ( targetList.size() == 0 )
		return false;
	else 
		return true;
}

