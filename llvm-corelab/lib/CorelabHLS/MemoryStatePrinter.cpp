#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/ADT/DenseMap.h"

#include "corelab/Analysis/PADriver.h"
#include "corelab/CorelabHLS/MemoryStatePrinter.h"
#include "corelab/Utilities/GetMemOper.h"

using namespace llvm;
using namespace corelab;

const static bool debug = true;

char MemoryStatePrinter::ID = 0;
static RegisterPass<MemoryStatePrinter> X("mem-state-print", 
		"Memory State Printer", false, false);

cl::opt<unsigned> memBitWidth(
		"memBitWidth", cl::init(0), cl::NotHidden,
		cl::desc("Define Memory BitWidth"));

cl::opt<bool> callPrint(
		"callPrint", cl::init(false), cl::NotHidden,
		cl::desc("Call Print"));

static int id;
static int getID(void) { return ++id; }

static int currentDataWidth;
static int currentNum;

static void resetCurrentData (void) {
	currentDataWidth = 0;
	currentNum = 0;
}

static bool updateCurrentData ( int width, int num ) {
	if ( currentNum == 0 ) {
		currentDataWidth = width;
		currentNum = num;
	}
	else {
		if ( currentDataWidth != width )
			return false;
		else
			currentNum += num;
	}
	return true;
}

void MemoryStatePrinter::getAnalysisUsage( AnalysisUsage &AU ) const
{
	AU.addRequired< PADriverTest >();
	AU.setPreservesAll();
}


void MemoryStatePrinter::setFunctions(void)
{
	LLVMContext &Context = module->getContext();
	printBits = module->getOrInsertFunction(
			"printBits",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context),//datawidth
			Type::getInt8PtrTy(Context),//char * ( name )
			Type::getInt8PtrTy(Context),//src
			Type::getInt32Ty(Context),//numofelements
			Type::getInt32Ty(Context),//memBitWidth
			Type::getInt1Ty(Context),
			Type::getInt32Ty(Context));

	printEnd = module->getOrInsertFunction(
			"printEnd",
			Type::getVoidTy(Context),
			Type::getInt8PtrTy(Context)); // function name

	registerStruct = module->getOrInsertFunction(
			"registerStruct",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context));

	startStruct = module->getOrInsertFunction(
			"startStruct",
			Type::getVoidTy(Context));

	addElement = module->getOrInsertFunction(
			"addElement",
			Type::getVoidTy(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context),
			Type::getInt32Ty(Context));
}

static Constant *geti8StrVal(Module& M, const char* str, Twine const& name){
	LLVMContext& Context = M.getContext();
	Constant* strConstant = ConstantDataArray::getString(Context, str);
	GlobalVariable* GVStr = 
		new GlobalVariable(M, strConstant->getType(), true, 
				GlobalValue::InternalLinkage, strConstant, name);
	Constant* zero = Constant::getNullValue(IntegerType::getInt32Ty(Context));
	Constant* indices[] = {zero, zero};
	Constant* strVal = ConstantExpr::getGetElementPtr(strConstant->getType(), GVStr, indices, true);
	return strVal;
}

struct MemoryInfo{
	unsigned dataWidth;
	unsigned numOfElements;
};

static MemoryInfo getMemoryInfo(Type *eTy)
{
	if ( SequentialType *aTy = dyn_cast<SequentialType>(eTy) )
	{
		MemoryInfo nowInfo;
		nowInfo.numOfElements = aTy->getNumElements();

		Type *nestType = aTy->getElementType();
		MemoryInfo nestInfo = getMemoryInfo( nestType );

		nowInfo.numOfElements *= nestInfo.numOfElements;
		nowInfo.dataWidth = nestInfo.dataWidth;

		return nowInfo;
	}
	else if (  IntegerType *iTy = dyn_cast<IntegerType>(eTy) )
	{
		MemoryInfo nowInfo;
		nowInfo.numOfElements = 1;
		nowInfo.dataWidth = iTy->getBitWidth();
		return nowInfo;
	}
	else if ( eTy->isFloatTy() )
	{
		MemoryInfo nowInfo;
		nowInfo.numOfElements = 1;
		nowInfo.dataWidth = 32;
		return nowInfo;
	}
	else if ( eTy->isDoubleTy() )
	{
		MemoryInfo nowInfo;
		nowInfo.numOfElements = 1;
		nowInfo.dataWidth = 64;
		return nowInfo;		
	}
	else if ( isa<PointerType>(eTy) )
	{
		MemoryInfo nowInfo;
		nowInfo.numOfElements = 1;
		//TODO : this should be deteremined by -like- :195-201 codes
		nowInfo.dataWidth = 32;
		//This is for x86 system
		//			nowInfo.dataWidth = DL->getPointerSizeInBits();
		return nowInfo;
	}
	else if ( StructType *sTy = dyn_cast<StructType>(eTy) )
	{
		MemoryInfo nowInfo;
		nowInfo.dataWidth = 0;
		nowInfo.numOfElements = 0;
		for ( int i = 0; i < sTy->getNumElements(); i++ )
		{
			MemoryInfo nestInfo = getMemoryInfo( sTy->getElementType(i) );
			if ( nowInfo.dataWidth < nestInfo.dataWidth )
				nowInfo.dataWidth = nestInfo.dataWidth;
			nowInfo.numOfElements += nestInfo.numOfElements;
		}
		return nowInfo;
	}
	else
	{
		eTy->dump();
		assert(0 &&"can not support this type of memory");
	}
}

void MemoryStatePrinter::insertRuntime(int memID, Instruction *insertPoint) {
	LLVMContext &Context = module->getContext();
	std::vector<Value *> actuals(0);
	actuals.clear();
	actuals.resize(3);
	actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), memID);
	actuals[1] = ConstantInt::get(Type::getInt32Ty(Context), currentDataWidth);
	actuals[2] = ConstantInt::get(Type::getInt32Ty(Context), currentNum);

	CallInst::Create(addElement, actuals, "", insertPoint);
}


void MemoryStatePrinter::insertStructHandler(Type *ty, int memID, Instruction *insertPoint) {
	if ( StructType *sTy = dyn_cast<StructType>(ty) ) {
		for ( int i = 0; i < sTy->getNumElements(); i++ )
		{
			Type *eTy = sTy->getElementType(i);
			
			insertStructHandler(eTy, memID, insertPoint);
		}
	}
	else if ( SequentialType *aTy = dyn_cast<SequentialType>(ty) ){
		/*
		if ( haveStruct(ty) ) {
			ty->dump();
			assert(0 &&"cannot handle this now");
		}*/

		MemoryInfo memInfo = getMemoryInfo( ty );
		if ( !updateCurrentData( memInfo.dataWidth, memInfo.numOfElements ) ) {
			//insert
			insertRuntime(memID, insertPoint);

			resetCurrentData();
			updateCurrentData( memInfo.dataWidth, memInfo.numOfElements );
		}
	}
	else if (  IntegerType *iTy = dyn_cast<IntegerType>(ty) ){
		if ( !updateCurrentData( iTy->getBitWidth(), 1 ) ) {
			//insert
			insertRuntime(memID, insertPoint);

			resetCurrentData();
			updateCurrentData( iTy->getBitWidth(), 1 );
		}
	}
	else if ( ty->isFloatTy() ){
		if ( !updateCurrentData( 32 , 1 ) ) {
			//insert
			insertRuntime(memID, insertPoint);

			resetCurrentData();
			updateCurrentData( 32 , 1 );
		}
	}
	else if ( ty->isDoubleTy() ){
		if ( !updateCurrentData( 64 , 1 ) ) {
			//insert
			insertRuntime(memID, insertPoint);

			resetCurrentData();
			updateCurrentData( 64 , 1 );
		}

	}
	else if ( isa<PointerType>(ty) ){ // define pointerWidth 2
		if ( !updateCurrentData( 2 , 1 ) ) {
			//insert
			insertRuntime(memID, insertPoint);

			resetCurrentData();
			updateCurrentData( 2 , 1 );
		}
	}

}

void MemoryStatePrinter::apply2Functions(void) {
	LLVMContext &Context = module->getContext();

	Function *mainF = module->getFunction("main");
	Instruction *firstI = &*((&*(mainF->begin()))->begin());

	for ( auto fi = module->begin(); fi != module->end(); fi++ ) 
	{
		Function *func = &*fi;

		if ( func->isDeclaration() ) continue;

		set<Instruction *> insertionPoints;
		insertionPoints.clear();

		set<Value *> printMemories;
		printMemories.clear();

//		DenseMap<Value *, Constant *> mem2name;
//		mem2name.clear();

		for ( auto bi = (&*fi)->begin(); bi != (&*fi)->end(); bi++ )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ii++ )
			{
				Instruction *inst = &*ii;

				if ( isa<ReturnInst>(inst) )
					insertionPoints.insert(inst);
				else if ( isa<LoadInst>(inst) || isa<StoreInst>(inst) ) {
					Value *ptr = getMemOper(inst);
					assert(ptr);

					set<Value *> memories = pa->getPointedMemory(ptr);
					assert( memories.size() != 0 );
					for ( auto iter : memories )
					{
						if ( isa<AllocaInst>(iter) )
							assert(0 && "Memory Object should be a global variable");

						printMemories.insert(iter);
					}
				}
			}

		//create function name str in global
		errs() << "try to create " << func->getName() << " name\n";
		Value *func_name = geti8StrVal(*module, (func->getName().str()).c_str(), func->getName());

		//create memory name str in global
		for ( auto memory : printMemories )
		{
			if ( mem2name.count(memory) )
				continue;

			mem2name[memory] = 
				geti8StrVal(*module, (memory->getName().str()).c_str(), memory->getName());

			if ( haveStruct(memory->getType()) ) {
				mem2id[memory] = getID();

				std::vector<Value *> actuals(0);
				actuals.clear();
				actuals.resize(1);
				actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), mem2id[memory]);
				CallInst::Create(registerStruct, actuals, "", firstI);

				actuals.clear();
				actuals.resize(0);
				CallInst::Create(startStruct, actuals, "", firstI);

				PointerType *pointerTy = dyn_cast<PointerType>(memory->getType());
				assert(pointerTy);
				Type *elementTy = pointerTy->getElementType();

				if ( SequentialType *sTy = dyn_cast<SequentialType>(elementTy) ) {
					actuals.clear();
					actuals.resize(3);
					actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), mem2id[memory]);
					actuals[1] = ConstantInt::get(Type::getInt32Ty(Context), 0);
					actuals[2] = ConstantInt::get(Type::getInt32Ty(Context), 0);
					CallInst::Create(addElement, actuals, "", firstI);
				}

				resetCurrentData();
				insertStructHandler(getFirstStruct(elementTy), mem2id[memory], firstI);
				if ( currentNum != 0 ) {
					insertRuntime( mem2id[memory], firstI );
					resetCurrentData();
				}

				//end sig
				actuals.clear();
				actuals.resize(3);
				actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), mem2id[memory]);
				actuals[1] = ConstantInt::get(Type::getInt32Ty(Context), 0);
				actuals[2] = ConstantInt::get(Type::getInt32Ty(Context), 0);
				CallInst::Create(addElement, actuals, "", firstI);
			}
		}


		for ( auto point : insertionPoints )
		{
			std::vector<Value *> actuals(0);

			//Function End Print
			actuals.resize(1);
			actuals[0] = func_name;
			CallInst::Create(printEnd, actuals, "", point);

			if ( !callPrint ) {

			//memory print
			for ( auto memory : printMemories )
			{
				PointerType *pTy = dyn_cast<PointerType>(memory->getType());
				assert(pTy);
				MemoryInfo mInfo = getMemoryInfo(pTy->getElementType());

				BitCastInst *ptrVal = 
					new BitCastInst(memory, Type::getInt8PtrTy(Context), "", point); 

				actuals.clear();
				actuals.resize(7);
				actuals[0] = ConstantInt::get(Type::getInt32Ty(Context), mInfo.dataWidth);
				actuals[1] = mem2name[memory];
				actuals[2] = ptrVal;
				if ( mem2id.count(memory) ) {
					PointerType *pointerTy = dyn_cast<PointerType>(memory->getType());
					assert(pointerTy);
					Type *elementTy = pointerTy->getElementType();
					if ( SequentialType *sTy = dyn_cast<SequentialType>(elementTy) ) {
						int elementsOfStructs = (getMemoryInfo(getFirstStruct(elementTy))).numOfElements;
						int numOfStructs = mInfo.numOfElements / elementsOfStructs;
						actuals[3] = ConstantInt::get(Type::getInt32Ty(Context), numOfStructs);
					}
					else
						actuals[3] = ConstantInt::get(Type::getInt32Ty(Context), mInfo.numOfElements);
				}
				else
					actuals[3] = ConstantInt::get(Type::getInt32Ty(Context), mInfo.numOfElements);
				actuals[4] = ConstantInt::get(Type::getInt32Ty(Context), memBitWidth);
				if ( mem2id.count(memory) ) { //struct
					actuals[5] = ConstantInt::get(Type::getInt1Ty(Context), true);
					actuals[6] = ConstantInt::get(Type::getInt32Ty(Context), mem2id[memory]);
				}
				else {
					actuals[5] = ConstantInt::get(Type::getInt1Ty(Context), false);
					actuals[6] = ConstantInt::get(Type::getInt32Ty(Context), 0);
				}

				CallInst::Create(printBits, actuals, "", point);
			}

			}

		}
	}

}

bool MemoryStatePrinter::runOnModule(Module& M) {

	module = &M;
	pa = getAnalysis< PADriverTest >().getPA();

	id = 0;

	setFunctions();

	apply2Functions();


	return true;
}


