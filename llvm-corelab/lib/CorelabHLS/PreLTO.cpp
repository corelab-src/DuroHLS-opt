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
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "IntrinsicLower"

using namespace llvm;
using namespace std;

namespace corelab {

	static unsigned getByteSize(Type *elementTy) {
		unsigned byteSize = 0;
//		if ( ArrayType *aTy = dyn_cast<ArrayType>(elementTy) )
		if ( SequentialType *aTy = dyn_cast<SequentialType>(elementTy) )
			return getByteSize(aTy->getElementType());
		else if ( IntegerType *iTy = dyn_cast<IntegerType>(elementTy) )
			byteSize = (iTy->getBitWidth())/8;
		else if ( elementTy->isFloatTy() )
			byteSize = 4;
		else if ( elementTy->isDoubleTy() )
			byteSize = 8;
		else if ( isa<PointerType>(elementTy) )
			byteSize = 2;
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

struct IntrinsicLower : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    IntrinsicLower() : FunctionPass(ID) {}

    virtual bool doInitialization(Module &M) {
       Mod = &M;

       TD = new DataLayout(&M);

       IL = new IntrinsicLowering(*TD);
       return false;
    }

    virtual bool runOnFunction(Function &F) {
        bool modified = false;

        // Examine all the instructions in this function to find the intrinsics
        // that need to be lowered.
        for (Function::iterator BB = F.begin(), EE = F.end(); BB != EE; ++BB) {
            bool localChange = true;

            // the below functions can modify instruction CI invalidating the
            // iterator so we need to keep checking the BB until there are no
            // more changes
            while (localChange) {
                localChange = false;
                for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I !=
                        E; ++I) {
                    if (CallInst *CI = dyn_cast<CallInst>(&*I)) {

                        Function *calledFunction = CI->getCalledFunction();
                        // ignore indirect function calls
                        if (!calledFunction) continue;

                        localChange = lowerIfIntrinsic(CI, calledFunction);

                        // recheck the BB again
                        if (localChange) {
                            modified = true;
                            break;
                        }
                    }
                }
            }
        }

        return modified;
    }

private:
    Module *Mod;
    IntrinsicLowering *IL;
    const DataLayout* TD;
    bool lowerIfIntrinsic(CallInst *CI, Function *calledFunction);
    bool lowerIntrinsic(CallInst *CI, Function *calledFunction);
    bool lowerBswapIntrinsic(CallInst *CI, Function *calledFunction);
    bool lowerOverflowIntrinsic(CallInst *CI, Function *calledFunction);
    void createOverflowSumCarry(CallInst *CI, Instruction* &sum,
            Instruction* &carry);
    void replaceOverflowIntrinsic(CallInst *CI, Instruction *sum, Instruction
            *carry);

    std::string getIntrinsicMemoryAlignment(CallInst *CI);
};

} // end of namespace

using namespace corelab;

CallInst *ReplaceCallWith(const char *NewFn, CallInst *CI,
								 vector<Value*> Args,
                                 Type *RetTy) {
	// If we haven't already looked up this function, check to see if the
	// program already contains a function with this name.
	Module *M = CI->getParent()->getParent()->getParent();
	// Get or insert the definition now.
	std::vector<Type *> ParamTys;
	std::vector<Value*> Params;
	if (!Args.empty()) {
		for (vector<Value*>::iterator it = Args.begin(); it != Args.end(); ++it) {
			ParamTys.push_back((*it)->getType());
		}
	}

	Constant* FCache = M->getOrInsertFunction(NewFn, FunctionType::get(RetTy, ParamTys, false));

	Instruction * Ins = CI;
	CallInst *NewCI = CallInst::Create(FCache, Args, "", Ins);
	NewCI->setName(CI->getName());
	if (!CI->use_empty()) {
		CI->replaceAllUsesWith(NewCI);
	}

	// CI doesn't get erased, so do it explicitly
	CI->eraseFromParent();
	return NewCI;
}


/// This is used to keep track of intrinsics that get generated to a lowered
/// function. We must generate the prototypes before the function body which
/// will only be expanded on first use (by the loop below).
/// Note: This function was adapted from lowerIntrinsics() in CBackend.cpp
bool IntrinsicLower::lowerIfIntrinsic(CallInst *CI, Function *calledFunction) {

    switch (calledFunction->getIntrinsicID()) {
      case Intrinsic::not_intrinsic:
      //case Intrinsic::memory_barrier: # replaced by fence atomic
      case Intrinsic::vastart:
      case Intrinsic::vacopy:
      case Intrinsic::vaend:
      case Intrinsic::returnaddress:
      case Intrinsic::frameaddress:
      case Intrinsic::setjmp:
      case Intrinsic::longjmp:
      case Intrinsic::prefetch:
      case Intrinsic::powi:
      case Intrinsic::x86_sse_cmp_ss:
      case Intrinsic::x86_sse_cmp_ps:
      case Intrinsic::x86_sse2_cmp_sd:
      case Intrinsic::x86_sse2_cmp_pd:
      case Intrinsic::ppc_altivec_lvsl:
          // We directly implement these intrinsics
          return false;

      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
    	  // This is just debug info - it doesn't affect program
    	  // behavior
    	  return false;

			case Intrinsic::bswap:
				return lowerBswapIntrinsic(CI, calledFunction);

      case Intrinsic::memcpy:
      case Intrinsic::memmove:
      case Intrinsic::memset:
          return lowerIntrinsic(CI, calledFunction);

      case Intrinsic::uadd_with_overflow:
          return lowerOverflowIntrinsic(CI, calledFunction);

      default:

          // All other intrinsic calls we must lower.
          errs() << "Lowering: " << *CI << "\n";
          IL->LowerIntrinsicCall(CI);

          return true;
    }

}

// determine the type of the memcpy, memmove, memset destination pointer
const Type *get_dest_ptr_type(CallInst *CI) {
    const Value *destOp = CI->getOperand(0);
    PointerType *destPtr;
    if (const BitCastInst *bci = dyn_cast<BitCastInst>(destOp)) {
        destPtr = dyn_cast<PointerType>(bci->getSrcTy());
    } else if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(
                destOp)) {
        destPtr = dyn_cast<PointerType>(gep->getPointerOperand()->getType());
    } else if (PointerType *p = dyn_cast<PointerType>(destOp->getType())) {
        destPtr = p;
    } else {
        errs() << "CallInst: " << *CI << "\n";
        errs() << "Dest Pointer: " << *destOp << "\n";
        llvm_unreachable("Unknown pointer destination in intrinsic argument");
    }

    const Type *destType = destPtr->getElementType();
    while (const ArrayType *at = dyn_cast<ArrayType>(destType)) {
        destType = at->getElementType();
    }
    return destType;
}

static unsigned getMaxBitWidth(const Type *ty) {
	if (const ArrayType *at = dyn_cast<ArrayType>(ty))
		return getMaxBitWidth(at->getElementType());
	else if (const StructType *st = dyn_cast<StructType>(ty)) {
		unsigned max = 0;
		for ( unsigned i = 0; i < st->getNumElements(); i++ )
		{
			Type *elementTy = st->getElementType(i);
			unsigned elementWidth = getMaxBitWidth(elementTy);
			if ( max < elementWidth )
				max = elementWidth;
		}
		return max;
	}
	else if (const  IntegerType *it = dyn_cast<IntegerType>(ty))
		return it->getBitWidth();
	else if (ty->isFloatTy())
		return 32;
	else if (ty->isDoubleTy())
		return 64;
	else if (isa<PointerType>(ty))
		return 32;
	else {
		ty->dump();
		assert(0);
	}
}

std::string IntrinsicLower::getIntrinsicMemoryAlignment(CallInst *CI) {

		std::string alignSize;

    const Type *destType = get_dest_ptr_type(CI);

    // use modified struct size if first argument is a struct pointer
    if (isa<StructType>(destType)) {
        // Get the alignment, and decide which memcpy to use
			errs() << "struct!!\n";

			Value *destOp = CI->getOperand(0);
			if ( GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(destOp) ) {
				Value *offsetV = gep->getOperand(2); // 0 : ptr, 1: 0, 2: offset
				assert(offsetV);
				ConstantInt *cInt = dyn_cast<ConstantInt>(offsetV);
				assert(cInt);
				int offsetInteger = cInt->getSExtValue();
				Type *newDestType = 
					dyn_cast<StructType>(destType)->getElementType(offsetInteger);
				assert(newDestType);

				unsigned newAlignSize = getByteSize(newDestType);
				alignSize = utostr(newAlignSize);
			}
			else if ( BitCastInst *bInst = dyn_cast<BitCastInst>(destOp) ) {
				alignSize = utostr( getMaxBitWidth(destType) / 8 );
//				alignSize = utostr(1);
			}
			else {
				ConstantInt *align = dyn_cast<ConstantInt>(CI->getOperand(3));
				for (int i = 8; i > 0; i >>= 1) {
					if (align->equalsInt(i)) {
						alignSize = utostr(i);
					}
				}
			}
    } else if (const IntegerType *it = dyn_cast<IntegerType>(destType)) {
        alignSize = utostr(it->getBitWidth() / 8);
    } else if (isa<PointerType>(destType)) {
        // pointers are 32 bits
        alignSize = "4";
    } else if (destType->isFloatTy()) {
        // floats are 32 bits
        alignSize = "4";
    } else if (destType->isDoubleTy()) {
        // double are 64 bits
        alignSize = "8";
    } else {
        errs() << "CallInst: " << *CI << "\n";
        llvm_unreachable("unknown destination pointer type");
    }

    return alignSize;
}

void IntrinsicLower::createOverflowSumCarry(CallInst *CI, Instruction* &sum,
        Instruction* &carry) {
    Value *op0 = CI->getArgOperand(0);
    Value *op1 = CI->getArgOperand(1);
    unsigned size = op0->getType()->getPrimitiveSizeInBits();
    unsigned newSize = size + 1;

    IntegerType * newType = IntegerType::get(Mod->getContext(),
            newSize);

    string name = "overflow_intrinsic";
    // assume llvm.uadd.* so we zero extend
    Instruction *zextOp0 = CastInst::CreateZExtOrBitCast(op0, newType,
            name, CI);
    Instruction *zextOp1 = CastInst::CreateZExtOrBitCast(op1, newType,
            name, CI);

    Instruction *add = BinaryOperator::Create(Instruction::Add, zextOp0,
            zextOp1, name, CI);

    sum = CastInst::CreateTruncOrBitCast(add, op0->getType(),
            name + "_sum", CI);

    APInt ap1 = APInt(newSize, size);
    Constant *one = ConstantInt::get(newType, ap1);
    Instruction *shift = BinaryOperator::Create(Instruction::LShr, add,
            one, name, CI);

    IntegerType * sizeOne = IntegerType::get(Mod->getContext(), 1);
    carry = CastInst::CreateTruncOrBitCast(shift, sizeOne,
            name + "_carry", CI);

}

void IntrinsicLower::replaceOverflowIntrinsic(CallInst *CI, Instruction *sum,
        Instruction *carry) {


    // find sum/carry extractvalue
    // sum: %108 = extractvalue %0 %uadd.i, 0
    // carry: %109 = extractvalue %0 %uadd.i, 1
    vector<Instruction*> dead;
    for (Value::user_iterator i = CI->user_begin(), e = CI->user_end(); i !=
            e; ++i) {
        if (ExtractValueInst *EV = dyn_cast<ExtractValueInst>(*i)) {
            Instruction *replace = NULL;
            // sum
            if (*EV->idx_begin() == 0) {
                replace = sum;
            // carry
            } else {
                assert(*EV->idx_begin() == 1);
                replace = carry;
            }
            errs() << "Replacing " << *EV << "\n";
            errs() << "With " << *replace << "\n";
            EV->replaceAllUsesWith(replace);
            dead.push_back(EV);

        } else {
            errs() << **i << "\n";
            llvm_unreachable("Expecting extractvalue for overflow intrinsic\n");
        }
    }

    // remove dead extractvalue instructions
    for (vector<Instruction*>::iterator i = dead.begin(), e =
            dead.end(); i != e; ++i) {
        (*i)->eraseFromParent();
    }
    CI->eraseFromParent();
}


// Handle uadd.with.overflow.* intrinsics. For example:
//      %uadd.i = call %0 @llvm.uadd.with.overflow.i64(i64 %105, i64 %106)
//      %108 = extractvalue %0 %uadd.i, 0
//      %109 = extractvalue %0 %uadd.i, 1
// Replace this n-bit addition with a (n + 1) bit addition and shift out the
// carry bit manually
bool IntrinsicLower::lowerOverflowIntrinsic(CallInst *CI, Function *calledFunction) {

    errs() << "Lowering overflow intrinsic: " << *CI << "\n";


    Instruction *sum = 0, *carry = 0;

    createOverflowSumCarry(CI, sum, carry);

    replaceOverflowIntrinsic(CI, sum, carry);

    return true;
}

bool IntrinsicLower::lowerBswapIntrinsic(CallInst *CI, Function *calledFunction) {
	CI->dump();
	calledFunction->dump();

	Value *op_v = CI->getArgOperand(0);
	op_v->dump();

	std::string functionName;
	functionName = "corelab_bswap";	

	if (op_v->getType()->isIntegerTy(64))
		functionName += "_i64";
	else if (op_v->getType()->isIntegerTy(32))
		functionName += "_i32";
	else if (op_v->getType()->isIntegerTy(16))
		functionName += "_i16";

	vector<Value *> Ops;
	Ops.push_back(CI->getOperand(0));

	errs() << "Replacing calls with: " << functionName << "\n";
	ReplaceCallWith(functionName.c_str(), CI, Ops,
			calledFunction->getReturnType());

	return true;
}

// handle memcpy, memmove, memset by replacing with specific functions
bool IntrinsicLower::lowerIntrinsic(CallInst *CI, Function *calledFunction) {

	//memcpy for initialization -> return false;

	errs() << "Try to Lowering : \n";
	CI->dump();
	calledFunction->dump();

	//XXX: Why this part need
	/*
	Value *op_v = CI->getArgOperand(1); // source
	op_v->dump();

	if ( isa<ConstantInt>( op_v ) ) 
	{
//		op_v->dump();
		errs() << "Source is Constant\n";
	}
	else if (User *op_i = dyn_cast<User>(op_v))
	{
//		op_i->dump();
		errs() << "Source is User\n";
		op_v = op_i->getOperand(0); // usually it is GEP Inst, so this operand 0 is pointer V

		if ( GlobalVariable *gv = dyn_cast<GlobalVariable>(op_v) )
			if ( gv->getInitializer() )
			{
				CI->eraseFromParent();
				return true;
			}
	}
	*/

//	errs() << "Lowering Start\n";

	std::string functionName;
	switch (calledFunction->getIntrinsicID()) {
		case Intrinsic::memcpy:
			functionName = "corelab_memcpy";
			break;
		case Intrinsic::memmove:
			functionName = "corelab_memmove";
			break;
		case Intrinsic::memset:
			functionName = "corelab_memset";
			break;
	}

	std::string fullFunctionName =
		functionName + "_" + getIntrinsicMemoryAlignment(CI);

	// we only need the first 3 operands
	vector<Value *> Ops;
	Ops.push_back(CI->getOperand(0));
	Ops.push_back(CI->getOperand(1));
	////////////
	/*
	unsigned changedWidth = getMaxBitWidth( get_dest_ptr_type(CI) );
	unsigned originalWidth = 
		getMaxBitWidth(dyn_cast<PointerType>(CI->getOperand(0)->getType())->getElementType());

	if ( changedWidth != originalWidth ) {
		assert( changedWidth % originalWidth == 0 );
		unsigned mul = changedWidth / originalWidth;

		ConstantInt *cInt = dyn_cast<ConstantInt>(CI->getOperand(2));
		assert(cInt);
		unsigned size_operand = cInt->getZExtValue();
		errs() << "Size is Changed\n";
		errs() << originalWidth<<" : "<<changedWidth<<" : "<<size_operand<<" : "<<mul<<"\n";
		assert( size_operand % mul == 0);

		LLVMContext &Context = calledFunction->getParent()->getContext();
		unsigned size_new = size_operand / mul;
		Value *newOperand = ConstantInt::get(Type::getInt64Ty(Context), size_new);

		Ops.push_back(newOperand);
	}
	else*/
		Ops.push_back(CI->getOperand(2));
	///////////

	if (CI->getOperand(2)->getType()->isIntegerTy(64))
		fullFunctionName += "_i64";

	errs() << "Replacing calls with: " << fullFunctionName << "\n";
	ReplaceCallWith(fullFunctionName.c_str(), CI, Ops,
			calledFunction->getReturnType());

	return true;
}
char IntrinsicLower::ID = 0;
static RegisterPass<IntrinsicLower> X("intrinsic-lower",
        "lower intrinsics to customized functions");
