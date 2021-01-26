#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DerivedTypes.h"

#include <list>

using namespace llvm;
using namespace std;

namespace corelab {

	class Stack2Static : public FunctionPass {
		public:
			static char ID;
			Stack2Static() : FunctionPass(ID) {}

			virtual bool runOnFunction(Function &F) {
				bool modified = false;

				modified = searchStackInst(&F);

				if ( modified ) {
					allocaStackSpace(&F);
					transformIntrinsic(&F);
				}

				return modified;
			}

		private:
			bool searchStackInst(Function *);
			void allocaStackSpace(Function *);
			void transformIntrinsic(Function *);

			DenseMap<Function *, list<CallInst *>> saveInstList;
			DenseMap<Function *, list<CallInst *>> restoreInstList;
			DenseMap<CallInst *, AllocaInst *> save2Alloca;

			DenseMap<Function *, AllocaInst *> func2frame;

			DenseMap<Function *, AllocaInst *> fcn2position;
			DenseMap<Function *, AllocaInst *> fcn2space;
			DenseMap<Function *, Instruction *> fcn2offset;
			DenseMap<Function *, Instruction *> fcn2baseAddr;
	};
}

using namespace corelab;

char Stack2Static::ID = 0;
static RegisterPass<Stack2Static> X("stack2static",
        "Intrinsic Stack Instruction to Static Alloca");

cl::opt<unsigned> StackSize(
		"stackSize", cl::init(100), cl::NotHidden,
		cl::desc("Define Stack Nested Size"));


	bool Stack2Static::searchStackInst(Function *F) {
		bool modified = false;

		for ( auto bi = F->begin(); bi != F->end(); ++bi )
			for ( auto ii = (&*bi)->begin(); ii != (&*bi)->end(); ++ii )
				if ( CallInst *cInst = dyn_cast<CallInst>(&*ii) ) {
					Function *calledFunction = cInst->getCalledFunction();

					if ( !calledFunction ) continue;

					if ( calledFunction->getIntrinsicID() == Intrinsic::stacksave ) {
						(saveInstList[F]).push_back(cInst);

						if ( func2frame.count(F) ) {
							save2Alloca[cInst] = func2frame[F];
						}
						else {
							Instruction *nextInst = &*(++ii);
							ii--;
							AllocaInst *allocaInst = dyn_cast<AllocaInst>(nextInst);
							assert(allocaInst);

							func2frame[F] = allocaInst;
							save2Alloca[cInst] = allocaInst;
						}

							modified = true;
					}

					if ( calledFunction->getIntrinsicID() == Intrinsic::stackrestore ) {
						(restoreInstList[F]).push_back(cInst);

						modified = true;
					}
				}

		return modified;
	}

	void Stack2Static::allocaStackSpace(Function *F) {
		AllocaInst *stackEntry = save2Alloca[(saveInstList[F]).front()];
		Type *entryType = stackEntry->getAllocatedType();
		ArrayType *stackSpaceType = ArrayType::get(entryType, StackSize);

		BasicBlock &entryBB = F->getEntryBlock();
		Instruction *firstInst = &*(entryBB.begin());

		LLVMContext &ctx = F->getParent()->getContext();
		const DataLayout &DL = F->getParent()->getDataLayout();

		IntegerType *position = IntegerType::get(ctx, 32);
		AllocaInst *positionAlloca = new AllocaInst(position, DL.getAllocaAddrSpace(),
				"StackPosition", firstInst);

		StoreInst *positionInit = new StoreInst(ConstantInt::get(Type::getInt32Ty(ctx), 0), 
				positionAlloca, firstInst);

		AllocaInst *stackSpaceAlloca = new AllocaInst(stackSpaceType, DL.getAllocaAddrSpace(),
				ConstantInt::get(Type::getInt32Ty(ctx), 1), "StackSpace", firstInst);

		Type *aTy = dyn_cast<PointerType>(stackSpaceAlloca->getType())->getElementType();
		Type *eTy = dyn_cast<ArrayType>(aTy)->getElementType();

		std::vector<Value *> actuals(0);
		actuals.resize(2);
		actuals[0] = ConstantInt::get(Type::getInt32Ty(ctx), 0);
		actuals[1] = ConstantInt::get(Type::getInt32Ty(ctx), 1);
		GetElementPtrInst *nextAddr = GetElementPtrInst::Create(
				dyn_cast<PointerType>(stackSpaceAlloca->getType())->getElementType(),
				stackSpaceAlloca, actuals, "NextAddr", firstInst);

		PtrToIntInst *toInt0 =
			new PtrToIntInst(stackSpaceAlloca, Type::getInt32Ty(ctx), "ToInt0", firstInst);
		PtrToIntInst *toInt1 =
			new PtrToIntInst(nextAddr, Type::getInt32Ty(ctx), "ToInt0", firstInst);

		Instruction *addrOffset = 
			BinaryOperator::Create(Instruction::Sub, toInt1, toInt0, "offset", firstInst);

		fcn2position[F] = positionAlloca;
		fcn2space[F] = stackSpaceAlloca;
		fcn2offset[F] = addrOffset;
		fcn2baseAddr[F] = toInt0;
	}

	void Stack2Static::transformIntrinsic(Function *F) {
		LLVMContext &ctx = F->getParent()->getContext();

		for ( auto saveInst : saveInstList[F] )
		{
			AllocaInst *position = fcn2position[F];
			AllocaInst *stack = fcn2space[F];

			AllocaInst *frameAlloca = save2Alloca[saveInst];
			assert(frameAlloca);

			LoadInst *positionLoad = new LoadInst(position, "GetPosition", saveInst);
			std::vector<Value *> actuals(0);
			actuals.resize(2);
			actuals[0] = ConstantInt::get(Type::getInt32Ty(ctx), 0);
			actuals[1] = positionLoad;

			Type *arrayType = dyn_cast<PointerType>(stack->getType())->getElementType();
			Type *frameType = dyn_cast<ArrayType>(arrayType)->getElementType();

			GetElementPtrInst *currentPosition = GetElementPtrInst::Create( arrayType, 
					stack, actuals, "CurrentFrame", saveInst);

			BitCastInst *currentFrame = 
				new BitCastInst(currentPosition, PointerType::getUnqual(Type::getInt8Ty(ctx)), 
						"ToPointerType", saveInst);
			
			Instruction *nextPositionOffset =
				BinaryOperator::Create(Instruction::Add, positionLoad, 
						ConstantInt::get(Type::getInt32Ty(ctx), 1), "NextPosition", saveInst);

			StoreInst *positionStore = new StoreInst(nextPositionOffset, position, saveInst);

			actuals[1] = nextPositionOffset;
			
			GetElementPtrInst *nextPosition = GetElementPtrInst::Create( arrayType, 
					stack, actuals, "NextFrame", saveInst);

			if ( frameAlloca->getType() != nextPosition->getType() ) {
				BitCastInst *nextFrame = 
					new BitCastInst(nextPosition, frameAlloca->getType(), "ToFramePtr", saveInst);
				frameAlloca->replaceAllUsesWith(nextFrame);
				frameAlloca->eraseFromParent();
			}

			saveInst->replaceAllUsesWith(currentFrame);
//			frameAlloca->replaceAllUsesWith(nextFrame);

			saveInst->eraseFromParent();
//			frameAlloca->eraseFromParent();
		}

		for ( auto restoreInst : restoreInstList[F] )
		{
			AllocaInst *position = fcn2position[F];
			AllocaInst *stack = fcn2space[F];
			Instruction *offset = fcn2offset[F];
			Instruction *baseAddr = fcn2baseAddr[F];

			Value *operand = restoreInst->getOperand(0);

			PtrToIntInst *toInt = 
				new PtrToIntInst(operand,  Type::getInt32Ty(ctx), "ToInt", restoreInst);

			Instruction *calOffset = 
				BinaryOperator::Create(Instruction::Sub, toInt, baseAddr, "PositionOffset", restoreInst);

			Instruction *newPosition= 	
				BinaryOperator::Create(Instruction::UDiv, calOffset, offset, "NewPosition", restoreInst);

			StoreInst *positionRestore = new StoreInst(newPosition, position, restoreInst);

			restoreInst->eraseFromParent();
		}
	}


