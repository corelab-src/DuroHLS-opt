#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/CallSite.h"

#include "corelab/HLSTest/MemInit.h"
#include "corelab/Utilities/GetMemOper.h"

#include <iostream>
#include <fstream>
#include <iomanip>

#define LAST(k,n) ((k) & ((1<<(n))-1))
#define MID(k,m,n) LAST((k)>>(m),((n)-(m)))

using namespace llvm;
using namespace corelab;
using namespace std;

//const static bool debug = true;

char MemInitTest::ID = 0;
static RegisterPass<MemInitTest> X("meminit-test", 
		"memory initialization test", false, false);

cl::opt<unsigned> MemBitWidth(
		"membitwidth", cl::init(0), cl::NotHidden,
		cl::desc("Memory BitWidth ( Memory Block Size )"));

	static unsigned getNumOfElementsFromType(Type *ty)
	{
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
			ty->dump();
			assert(0 &&"can not support this type of memory");
			return 0;
		}
	}

void MemInitTest::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.setPreservesAll();
}

unsigned MemInitTest::getBitWidth(Type *ty) {
	if ( PointerType *pTy = dyn_cast<PointerType>(ty) )
		return getBitWidth(pTy->getElementType());
	else if ( IntegerType *iTy = dyn_cast<IntegerType>(ty) )
		return iTy->getBitWidth();
	else if ( ty->isFloatTy() )
		return 32;
	else if ( ty->isDoubleTy() )
		return 64;
	else if ( FunctionType *fTy = dyn_cast<FunctionType>(ty) )
		assert(0);
	else if ( SequentialType *sTy = dyn_cast<SequentialType>(ty) )
		return getBitWidth(sTy->getElementType());
	else if ( StructType *sTy = dyn_cast<StructType>(ty) ) {
		unsigned bitwidth = 0;
		for ( unsigned i = 0; i < sTy->getNumElements(); ++i )
			if ( bitwidth < getBitWidth(sTy->getElementType(i)) )
					bitwidth = getBitWidth(sTy->getElementType(i));
		return bitwidth;
	}
	else
		assert(0);
}

void MemInitTest::searchGV(void) {

//	const DataLayout &DL = module->getDataLayout();
	for (auto gvIter = module->global_begin(); gvIter != module->global_end(); gvIter++)
	{
		if ( (&*gvIter)->isDeclaration() ) continue;
		if ( (&*gvIter)->getName() == "llvm.used" ) continue;

		if ( const Constant *init = (&*gvIter)->getInitializer() ) {

			gvInitSet.insert(&*gvIter);
		}
	}

}

Value *MemInitTest::findSourceValue(Value *v) {
	if ( AllocaInst *aInst = dyn_cast<AllocaInst>(v) )
		return v;
	else if ( isa<GlobalVariable>(v) )
		return v;
	else if ( BitCastInst *bInst = dyn_cast<BitCastInst>(v) )
		return findSourceValue(bInst->getOperand(0));
	else if ( isa<BitCastOperator>(v) )
		return findSourceValue(dyn_cast<User>(v)->getOperand(0));
	else if ( GetElementPtrInst *gepInst = dyn_cast<GetElementPtrInst>(v) )
		return findSourceValue(gepInst->getOperand(0));
	else if ( isa<GEPOperator>(v) )
		return findSourceValue(dyn_cast<User>(v)->getOperand(0));
	else
		return NULL;
}

pair<GlobalVariable *, AllocaInst *> MemInitTest::findSourceGV(AllocaInst *aInst) {
	Function *F = aInst->getParent()->getParent();
	for ( auto bi = F->begin(); bi != F->end(); ++bi )
		for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
		{
			Instruction *inst = &*ii;

			if ( isa<StoreInst>(inst) || isa<LoadInst>(inst) ) {
				Value *ptr_v = getMemOper(inst);
				Value *ptr_origin = findSourceValue(ptr_v);
				if ( ptr_origin ) {
					if ( ptr_origin == aInst )
						return make_pair((GlobalVariable *)NULL, (AllocaInst *)NULL);
				}
				else
					return make_pair((GlobalVariable *)NULL, (AllocaInst *)NULL);
			}

			if ( CallInst *cInst = dyn_cast<CallInst>(inst) )
				if ( Function *callee = cInst->getCalledFunction() )
					if ( callee->isDeclaration() )
						if ( callee->getIntrinsicID() == 132 ) { //memcpy
							Value *dst = findSourceValue(cInst->getOperand(0));
							Value *src = findSourceValue(cInst->getOperand(1));
							Value *size = cInst->getOperand(2);
						
							if ( dst == NULL || src == NULL )
								continue;
							if ( !isa<GlobalVariable>(src) )
								continue;
							if ( dyn_cast<AllocaInst>(dst) != aInst )
								continue;

							PointerType *dst_ptr = dyn_cast<PointerType>(dst->getType());
							PointerType *src_ptr = dyn_cast<PointerType>(src->getType());
							assert(dst_ptr);
							assert(src_ptr);
							unsigned dst_num = getNumOfElementsFromType(dst_ptr->getElementType());
							unsigned src_num = getNumOfElementsFromType(src_ptr->getElementType());
							if ( dst_num != src_num )
								return make_pair((GlobalVariable *)NULL, (AllocaInst *)NULL);

							auto returnVal = make_pair(dyn_cast<GlobalVariable>(src), aInst);
							return returnVal;
						}
		}

	return make_pair((GlobalVariable *)NULL, (AllocaInst *)NULL);
}

void MemInitTest::searchAlloca(void) {

	for ( auto fi = module->begin(); fi != module->end(); fi++ )
		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;
				if ( AllocaInst *aInst = dyn_cast<AllocaInst>(inst) ) {
					auto gvPair = findSourceGV(aInst);
					GlobalVariable *sameGV = gvPair.first;
					if ( isInitGV(sameGV) ) {
						alloc2GV[aInst] = sameGV;
						erasedList.push_back(gvPair);
					}
				}
			}
}

void MemInitTest::insertMetadata(void) {
	LLVMContext &context = module->getContext();
	for ( auto iter : alloc2GV )
	{
		AllocaInst *aInst = iter.first;
		GlobalVariable *gv = iter.second;

		Constant *idV = ConstantInt::get(Type::getInt64Ty(context), gvID);
		Metadata *idM = (Metadata *)ConstantAsMetadata::get(idV);
		Metadata *valuesArray[] = {idM};
		ArrayRef<Metadata *> values(valuesArray, 1);
		MDNode *mdNode = MDNode::get(context, values);
		aInst->setMetadata("initGV", mdNode);
		gv2ID.push_back(make_pair(gv, gvID));
		gvID++;
	}

/*
	for ( auto iter : alloc2GV )
	{
		AllocaInst *aInst = iter.first;
		if ( MDNode *md = aInst->getMetadata("initGV") )
			if ( Metadata *m = md->getOperand(0).get() )
				if ( Value *idV = dyn_cast<ValueAsMetadata>(m)->getValue() )
					idV->dump();
	}
*/
}

void MemInitTest::genInitBinFile(void) {

	errs() << MemBitWidth << "mem bitwidth\n";
	assert(MemBitWidth % 8 == 0);

	for ( auto iter : gv2ID ) {
		GlobalVariable *gv = iter.first;
		unsigned id = iter.second;
		
		SmallString<256> nameBuf;
		nameBuf.clear();
		StringRef fileNameRef = Twine(Twine(id) + Twine(".mem")).toStringRef(nameBuf);
		ofstream output(fileNameRef.str(), ios::out | ios::trunc);
		if ( const Constant *init = gv->getInitializer() ) {
			Type *initType = init->getType();
			initType->dump();
			unsigned dataBitWidth = getBitWidth(initType);
			assert(MemBitWidth % dataBitWidth == 0);

			if ( const ConstantDataSequential *array = dyn_cast<ConstantDataSequential>(init) ) {
				unsigned m = array->getNumElements();

				if ( MemBitWidth != 0 ) {
					assert(MemBitWidth >= dataBitWidth);
					for ( unsigned k = 0; k < m; ++k )
					{
						for ( unsigned j = 0; j < dataBitWidth / 8; ++j )
						{
							auto pulled = MID(array->getElementAsInteger(k), j*8, (j+1)*8);
							output << std::setfill('0') << std::setw(2) << hex << pulled;
						}
						if ( ((k+1) * dataBitWidth) %  MemBitWidth == 0 )
							output << endl;
					}

					unsigned totalBitSize = m * dataBitWidth;
					unsigned numBlock = ((totalBitSize % MemBitWidth) == 0) ? 
						(totalBitSize / MemBitWidth) : ((totalBitSize / MemBitWidth) + 1);
					if ( totalBitSize % MemBitWidth != 0 ) {
						unsigned additionalBits = (numBlock * MemBitWidth) - totalBitSize;
						for ( unsigned i = 0; i < additionalBits / 8; ++i )
							output << std::setfill('0') << std::setw(2) << hex << 0;
					}
				}
				else {
					for ( unsigned i = 0; i < m; ++i )
					{
						uint64_t data = array->getElementAsInteger(i);
						output << hex << data << endl;
					}
				}
			}
			else if ( const ConstantArray *constArray = dyn_cast<ConstantArray>(init) ) {
				unsigned n = init->getNumOperands();

				if ( MemBitWidth != 0 ) {
					assert(MemBitWidth > dataBitWidth);
					
					unsigned totalNum = 0;
					for ( unsigned i = 0; i < n; ++i )
					{
						const Value *opV = init->getOperand(i);
						if ( const ConstantDataSequential *nestArray 
								= dyn_cast<ConstantDataSequential>(opV) ) {

							//XXX: Assume nest arrays have the same type
							unsigned m = nestArray->getNumElements();
							totalNum += m;

							for ( unsigned k = 0; k < m; ++k )
							{
								for ( unsigned j = 0; j < dataBitWidth /8; ++j )
								{
									auto pulled = MID( nestArray->getElementAsInteger(k), j*8, (j+1)*8 );
									output << std::setfill('0') << std::setw(2) << hex << pulled;
								}
								if ( (((k+1) + (i*m)) * dataBitWidth) % MemBitWidth == 0 )
									output << endl;
							}
						}
					}

					unsigned totalBitSize = totalNum * dataBitWidth;
					unsigned numBlock = ((totalBitSize % MemBitWidth) == 0) ? 
						(totalBitSize / MemBitWidth) : ((totalBitSize / MemBitWidth) + 1);
					if ( totalBitSize % MemBitWidth != 0 ) {
						unsigned additionalBits = (numBlock * MemBitWidth) - totalBitSize;
						for ( unsigned i = 0; i < additionalBits / 8; ++i )
							output << std::setfill('0') << std::setw(2) << hex << 0;
					}
				}
				else {
					for ( unsigned i = 0; i < n; ++i )
					{
						const Value *opV = init->getOperand(i);
						if ( const ConstantDataSequential *nestArray 
								= dyn_cast<ConstantDataSequential>(opV) ) {
							unsigned m = nestArray->getNumElements();
							Type *ty = nestArray->getElementType();
							for ( unsigned j = 0; j < m ; ++j )
							{
								uint64_t data = nestArray->getElementAsInteger(j);
								output << hex << data << endl;
							}
						}
					}
				}
			}
			/*
			else if ( const ConstantDataSequential *array = dyn_cast<ConstantDataSequential>(init) ) {
				StringRef fileNameBuf = Twine(Twine(id) + Twine(".mem")).toStringRef(nameBuf);
				unsigned m = array->getNumElements();
				IntegerType *ty = dyn_cast<IntegerType>(array->getElementType());
				for ( unsigned i = 0; i < m; ++i )
				{
					uint64_t data = array->getElementAsInteger(i);
					output << hex << data << endl;
				}
			}
			*/
		}
		output.close();
	}
}

bool MemInitTest::runOnModule(Module& M) {
	module = &M;
	gvID = 1;

	searchGV();

	searchAlloca();

//	insertMetadata();

//	genInitBinFile();

	/*
	printf("test\n\n");
	ifstream test("1.bin", ios::in | ios::binary);
	for ( unsigned i = 0; i < 60; i++ )
	{
		uint64_t testRead;
		test.read((char *)&testRead, sizeof(uint64_t));
		printf("\t%lx", testRead);
	}
	printf("test\n\n");
	test.close();
	*/
	
	if ( erasedList.size() == 0 )
		return false;
	else {
		errs() << "Replace alloc -> gloval\n";
		for ( auto iter : erasedList )
		{
			GlobalVariable *gV = iter.first;
			AllocaInst *aInst = iter.second;
			aInst->dump();
			gV->dump();
			if ( aInst->getType() == gV->getType() )
				aInst->replaceAllUsesWith(gV);
		}
		return true;
	}
}


