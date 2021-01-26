#define DEBUG_TYPE "rec2iter"

#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"

#include "corelab/CorelabHLS/Rec2Iter.h"
#include "corelab/Utilities/InstInsertPt.h"
#include "corelab/Utilities/LiveValues.h"

#include <set>

static bool debug = true;

namespace corelab
{
  using namespace llvm;

  char Rec2Iter::ID = 0;
  namespace
  {
    static RegisterPass<Rec2Iter> RP("rec2iter",
      "Translate recursive functions into loops",
      false, false);
  }

  cl::opt<std::string> TargetFunctionName(
    "rec2iter-target-fcn", cl::init(""), cl::NotHidden,
    cl::desc("Only translate recursion to iteration in this function"));

  cl::opt<bool> AllocateFramesOnHeap(
    "rec2iter-heap", cl::init(true), cl::NotHidden,
    cl::desc("Allocate activation records on the heap, not the stack"));

  cl::opt<bool> DemoteAllocasToHeap(
    "rec2iter-demote-alloca", cl::init(true), cl::NotHidden,
    cl::desc("When transforming a recursive function, convert alloca instructions into malloc/free"));

  STATISTIC(numFcns,      "Number of recursive functions converted");
  STATISTIC(numCallsites, "Number of recursive callsites converted");
  STATISTIC(numRemat,     "Number of live values rematerialized");
  STATISTIC(numDemote,    "Number of AllocaInsts demoted to malloc/free");

  bool Rec2Iter::runOnModule(Module &mod)
  {
    bool modified = false;

    // For each function in this module
    typedef Module::iterator FI;
    for(FI i=mod.begin(), e=mod.end(); i!=e; ++i)
    {
      Function &fcn = *i;

      if( fcn.isDeclaration() )
        continue;

      if(TargetFunctionName != "" && TargetFunctionName != fcn.getName())
        continue;

      modified |= runOnFunction(fcn);
    }

    return modified;
  }

  typedef std::vector<CallSite>         Recurrences;
  typedef Recurrences::iterator         RecIt;
  typedef std::vector<ReturnInst*>      FunctionExits;

  typedef LiveValues::ValueList         ValueList;
  typedef ValueList::iterator           VLI;
  typedef DenseMap<Value*,ValueList>    Callsite2ValueList;
  typedef Callsite2ValueList::iterator  C2VI;

  struct LiveValueInfo
  {
    int         ar_index;       // within the activation record
    BitVector   callsites;      // which callsites save this value?
    Value *     stackSlot;

    LiveValueInfo()
      : ar_index(0), callsites(), stackSlot(0)
    {}
  };

  typedef DenseMap<const Value*,LiveValueInfo> LiveValueInfoTable;
  typedef LiveValueInfoTable::iterator          LVIT;

  typedef Function::arg_iterator        ArgIt;

  // Collects all recursive callsites within this function.
  // Collects all exits of this function.
  // Returns true iff all callsites are simple tail recursive.
  static bool findRecurrences(
    Function &fcn, Recurrences &recurrences, FunctionExits &exits)
  {
    bool tailRecursive = true;

    // Visit every instruction in this function,
    // searching for recursive calls.
    for(inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      Instruction *inst = &*i;

      if( isa<CallInst>(inst) || isa<InvokeInst>(inst) )
      {
        CallSite cs(inst);

        if( cs.getCalledFunction() == &fcn )
        {
          recurrences.push_back( cs );

          // is it a proper tail recursion?
          // I.e. it is immediately followed by
          // a return statement, and if that
          // return statement returns anything,
          // it returns the return value of the
          // recursive call.
          inst_iterator next = i;
          if( next == e )
            tailRecursive = false;

          else
          {
            ++next;

            ReturnInst *ret = dyn_cast<ReturnInst>(&*next);
            if( !ret )
              tailRecursive = false;
            else if( ret->getReturnValue()
            && ret->getReturnValue() != inst )
              tailRecursive = false;
          }
        }
      }

      else if( isa<ReturnInst>(inst) )
        exits.push_back( cast<ReturnInst>(inst) );
    }

    return tailRecursive;
  }

  // Modify the function by splitting basic blocks
  // so that every recursive callsite is the first
  // instruction in its basic block.
  static void splitBlocksAtRecurrences(Recurrences &recurrences)
  {
    // Split basic blocks at the callsites,
    // and replace the call instruction with a
    // branch to the descend block.
    for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i)
    {
      CallSite &cs = *i;

      Instruction *inst = cs.getInstruction();
      BasicBlock *bb = inst->getParent();
      bb->splitBasicBlock( inst, bb->getName() + "_after" );
    }
  }

  static void findInductionVariables(Function &fcn,
    Recurrences &recurrences)
  {
    // Check each argument for induction variable...
    unsigned argno=0;
    for(ArgIt i=fcn.arg_begin(), e=fcn.arg_end(); i!=e; ++i, ++argno)
    {
      Argument *arg = &*i;

      // For each callsite
      for(RecIt j=recurrences.begin(), f=recurrences.end(); j!=f; ++j)
      {
        CallSite &cs = *j;
        Value *actual = cs.getArgument(argno);

        BinaryOperator *binop = dyn_cast< BinaryOperator >( actual );
        if( !binop )
          continue;

        // We can only handle invertible operations:
        //  Add, FAdd, Sub, FSub, Xor.
        // We cannot do multiplication, division or remainder
        // because division is not closed.
        // TODO: make negation (0-x) work.
        Instruction::BinaryOps op = binop->getOpcode();

        if( op == Instruction::Add
        ||  op == Instruction::FAdd
        ||  op == Instruction::Sub
        ||  op == Instruction::FSub
        ||  op == Instruction::Xor )
        { /* good */ }
        else
          continue;

        Constant *operand = 0;
        if( binop->getOperand(0) == arg )
          operand = dyn_cast< Constant >( binop->getOperand(1) );
        else if( binop->getOperand(1) == arg
        // Don't handle negation yet
        &&       op != Instruction::Sub
        &&       op != Instruction::FSub)
          operand = dyn_cast< Constant >( binop->getOperand(0) );

        if( !operand )
          continue;

        // At this callsite, we have an induction pattern (op,operand)
        
				if ( debug ) {
          errs() << "\t- The argument " << *arg
                 << " is an induction variable with "
                 << " pattern (" << op
                 << ", " << *operand
                 << ") at callsite " << *cs.getInstruction()
                 << ".\n";
				}

        // TODO:
        //  record this information somehow
        //  don't save IVs in the frame
        //  recompute around callsites, somehow.
      }
    }
  }


  static bool canRemat(const Instruction *lv, ValueList &liveValues);
  static bool isAvailable(const Value *lv, ValueList &liveValues)
  {
    if( isa<Constant>( lv ) )
      return true;
    if( std::find(liveValues.begin(), liveValues.end(), lv) != liveValues.end() )
      return true;

/* Recursion here will yield minimal improvement,
 * but greatly complicates the logic for rematerialization.
 * TODO: enable this
    if( canRemat( dyn_cast<Instruction>( lv ), liveValues) )
      return true;
*/

    return false;
  }

  static bool canRemat(const Instruction *lv, ValueList &liveValues)
  {
    // Some operations are constants, etc, and it doesn't make
    // sense to replicate them.  We only rematerialize instructions.
    if( !lv )
      return false;

    // Some operations are risky to replicate

    if( isa<PHINode>( lv ) ) // avoid values computed in loops
      return false;
    if( isa<AllocaInst>( lv ) )
      return false;
    if( isa<LoadInst>( lv ) )
      return false;
    if( isa<StoreInst>( lv ) )
      return false;
    if( isa<CallInst>( lv ) )
      return false;
    if( isa<InvokeInst>( lv ) )
      return false;

    // Ensure that all operands are available
    for(Instruction::const_op_iterator k=lv->op_begin(), g=lv->op_end(); k!=g; ++k)
      if( ! isAvailable(*k, liveValues) )
        return false;

    return true;
  }

  static void findRematOpportunities(Function &fcn,
    Recurrences &recurrences,
    Callsite2ValueList &liveValuesPerCallsite,
    Callsite2ValueList &rematOpportunities,
    Callsite2ValueList &stackSavePerCallsite)
  {
    // for each callsite
    for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i)
    {
      Instruction *callInst = i->getInstruction();

      ValueList &liveValues = liveValuesPerCallsite[callInst];
      ValueList &remat      = rematOpportunities[callInst];
      ValueList &stack      = stackSavePerCallsite[callInst];

			if ( debug ) {
      errs()
        << "\t- " << liveValues.size()
        << " values at callsite " << *callInst << ".\n";
				for ( auto vi : liveValues )
					vi->dump();
			}

      // for each live value at this callsite
      for(VLI j=liveValues.begin(), f=liveValues.end(); j!=f; ++j)
      {
        const Instruction *lv = dyn_cast<Instruction>( *j );

        if( canRemat(lv, liveValues) )
        {
          // This could be rematerialized!
					if ( debug ) {
          errs() << "\t\t- " << lv->getName()
                       << " will be rematerialized.\n";
					}
          ++numRemat;
          remat.push_back(lv);
        }
        else
        {
          // This must be saved to a stack slot.
          stack.push_back(lv);
        }
      }
    }
  }


  // Conservatively determine the set of values which are live
  // across any call site.  Store those into liveValuesPerCallsite
  //
  static void findLiveValuesPerCallsite(Function &fcn,
    Recurrences &recurrences, Callsite2ValueList &liveValuesPerCallsite)
  {
//    LiveValues lva(fcn,false);
    LiveValues lva(fcn,true);

    // Now that we have computed the OUT sets,
    // we can efficiently compute the live values
    // at each callsite.
    // Even better: because we split the basic blocks
    // at each callsite, the live values at each callsite
    // are PRECISELY the OUT[bb] sets!
    for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i)
    {
      Instruction *inst = i->getInstruction();

      lva.findLiveValuesAfterInst(
        inst, liveValuesPerCallsite[inst]);
    }
  }

  static StructType *makeFrameTy(
    Function &fcn,
    Recurrences &recurrences,
    Callsite2ValueList &stackSavePerCallsite,
    LiveValueInfoTable &liveValues)
  {
		if ( debug )
	    errs() << "struct Frame {\n";

    LLVMContext &ctx = fcn.getParent()->getContext();
    const bool multipleRecurrences = recurrences.size() > 1;

    // For convenience, give this type a name
    StringRef typeName = (StringRef("ActivationRecord_") + fcn.getName()).str();

    // The non-recursive structure type
    StructType *framety = StructType::create(ctx, typeName);

    // Return the llvm::Type for the activation record.
    // The activation record is a stack data structure
    // containing:
    //  1. A pointer to the previous frame
    //  2. A return specifier, signifying which callsite to return to.
    //  3. Caller-save live values.

    // ordered list of the types of each field
    // in the activation record.
    typedef std::vector<Type*> Types;
    Types membertys;

    membertys.push_back( PointerType::getUnqual(framety) );
		if ( debug )
	    errs() << "\tFrame*\tprevious.frame;\n";

    // return specifier
    Type *intty = Type::getInt32Ty(ctx);
    if( multipleRecurrences )
    {
      membertys.push_back( intty );
			if ( debug ) {
      errs() << '\t' << *membertys.back()
                   << '\t' << "return.spec;\n";
			}
    }

    if( !AllocateFramesOnHeap )
    {
      Type *byteptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
      membertys.push_back( byteptr );
			if ( debug )
	      errs() << "\tvoid*\told.sp;\n";
    }

    // Try to place non-incompatible caller-save
    // values into the same location.
    // This is a simple, greedy approximation to
    // the assignment problem.
    if( multipleRecurrences )
    {
      bool again;
      BitVector interference(recurrences.size());
      do
      {
        again = false;

        // ensure that no two values in this batch
        // interfere
        interference.reset();

        // this is where we will place this batch
        const unsigned position = membertys.size();

        // Choose at most one value from each callsite.
        unsigned cs_no=0;
        for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i, ++cs_no)
        {
          if( interference.test(cs_no) )
            continue;

          Value *callInst = i->getInstruction();
          ValueList &stackSave = stackSavePerCallsite[ callInst ];

          // Choose the first good stack-save value from this callsite.
          for(VLI j=stackSave.begin(), f=stackSave.end(); j!=f; ++j)
          {
            const Value *lv = *j;
            LiveValueInfo &lvi = liveValues[lv];

            if( lvi.ar_index > 0 )
              continue; // already assigned.

            if( lv->getType() != intty )
              continue; // common, compatible type.

            BitVector incompatible = interference;
            incompatible &= lvi.callsites;
            if( incompatible.any() )
              continue; // not compatible.

            // Cool, we found one
            interference |= lvi.callsites;

            if( !again )
            {
              membertys.push_back( intty );
							if ( debug )
	              errs() << '\t' << *membertys.back() << '\t';
            }
            else
							if ( debug )
              	errs() << ", ";

						if ( debug )
            	errs() << lv->getName();

/*
            DEBUG(
              int j=lvi.callsites.find_first();
              if( j >= 0 )
              {
                errs() << " (( " << j;
                j=lvi.callsites.find_next(j);
                while( j >= 0 )
                {
                  errs() << ", " << j;
                  j=lvi.callsites.find_next(j);
                }
                errs() << " ))";
              }
            );
*/

            lvi.ar_index = position; // assigned.
            again = true;
            break; // choose no more from this callsite.
          }
        }

        if( again )
					if ( debug )
	          errs() << ";\n";

      } while( again );
    }

    // Allocate a position for every stack-save value
    // which has not yet been assigned.
    for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i)
    {
      Value *callInst = i->getInstruction();

      // Allocate positions within the frame
      ValueList &stackSave = stackSavePerCallsite[ callInst ];
      for(VLI j=stackSave.begin(), f=stackSave.end(); j!=f; ++j)
      {
        const Value *lv = *j;
        LiveValueInfo &lvi = liveValues[lv];
        if( lvi.ar_index > 0)
          continue;

        // allocate a position in the frame
        liveValues[lv].ar_index = membertys.size();
        membertys.push_back( lv->getType() );
				if ( debug ) {
        errs() << '\t' << *membertys.back()
                     << '\t' << lv->getName()
                     << ";\n";
				}
      }
    }

		if ( debug )
	    errs() << "\t}\n";

    framety->setBody(membertys, true);

    return framety;
  }

  static Value *getMalloc(Module &mod)
  {
    LLVMContext &ctx = mod.getContext();
    Type *byteptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
    Type *int64ty = Type::getInt64Ty(ctx);
    Value *malloc = mod.getOrInsertFunction(
      "malloc", byteptr, int64ty);
    return malloc;
  }

  static Value *getFree(Module &mod)
  {
    LLVMContext &ctx = mod.getContext();
    Type *byteptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
    Value *free = mod.getOrInsertFunction(
      "free", Type::getVoidTy(ctx), byteptr);
    return free;
  }

  void Rec2Iter::demoteAllocasToHeap(Function &fcn)
  {
//    TargetData &td = getAnalysis<TargetData>();
		const DataLayout &td = fcn.getParent()->getDataLayout();

    Module *mod = fcn.getParent();
    Value *malloc = getMalloc(*mod);
    Value *free = getFree(*mod);

    LLVMContext &ctx = fcn.getParent()->getContext();
    Type *byteptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
    Type *int64ty = Type::getInt64Ty(ctx);

    // Foreach AllocaInst in the function entry block
    std::vector<Instruction *> toDelete;
    BasicBlock *entry = & fcn.getEntryBlock();
    for(BasicBlock::iterator i=entry->begin(), e=entry->end(); i!=e; ++i)
    {
      Instruction *inst = &*i;

      AllocaInst *alloca = dyn_cast< AllocaInst >(inst);
      if( !alloca )
        continue;

      ++numDemote;

      // How many bytes is this allocation?
      InstInsertPt allocpt = InstInsertPt::After(alloca);
      const unsigned elementSizeBytes = td.getTypeSizeInBits( alloca->getAllocatedType() )/8;

      // Possibly multiply by array size
      Value *size = ConstantInt::get(int64ty, elementSizeBytes);
      if( alloca->isArrayAllocation() )
      {
        Value *n = alloca->getArraySize();
        if( n->getType() != int64ty )
        {
          Instruction *cast = new SExtInst(n, int64ty);
          allocpt << cast;
          n = cast;
        }

        Instruction *mul = BinaryOperator::CreateNSW( Instruction::Mul, size, n );
        allocpt << mul;
        size = mul;
      }

      // Insert call to malloc
      Instruction *call = CallInst::Create(malloc, size);
      allocpt << call;

      Value *object = call;
      if( object->getType() != alloca->getType() )
      {
        Instruction *cast = new BitCastInst(object, alloca->getType() );
        allocpt << cast;

        object = cast;
      }

      object->takeName( alloca );
      alloca->replaceAllUsesWith(object);

      // Insert call(s) to free at all function exits
      for(Function::iterator j=fcn.begin(), z=fcn.end(); j!=z; ++j)
      {
        BasicBlock *bb = &*j;
        TerminatorInst *term = bb->getTerminator();
        if( isa< ReturnInst >(term) )
        {
          InstInsertPt freept = InstInsertPt::Before(term);

          Value *arg = object;
          if( arg->getType() != byteptr )
          {
            Instruction *cast = new BitCastInst(arg, byteptr);
            freept << cast;

            arg = cast;
          }

          freept << CallInst::Create(free, arg);
        }
      }

      // Remember to delete this later
      toDelete.push_back(alloca);
    }

    for(unsigned i=0, N=toDelete.size(); i<N; ++i)
      toDelete[i]->eraseFromParent();
  }


  bool Rec2Iter::runOnFunction(Function &fcn)
  {
    LLVMContext &ctx = fcn.getParent()->getContext();
    Module *mod = fcn.getParent();
		
    Type *intty = Type::getInt32Ty(ctx);
    Type *int64ty = Type::getInt64Ty(ctx);
    Constant *zero = ConstantInt::get(intty, 0UL, true);
    Constant *one = ConstantInt::get(intty, 1UL, true);
    Constant *two = ConstantInt::get(intty, 2UL, true);

    Recurrences recurrences;
    FunctionExits exits;

    const bool tailRecursive = findRecurrences(fcn, recurrences, exits);
    const bool multipleRecurrences = (recurrences.size() > 1);

    // No recursive calls? exit
    if( recurrences.empty() )
      return false;

		if ( debug ) {
	    errs() << "Rec2Iter enters " << fcn.getName() << ".\n";
  	  errs() << "\t- " << recurrences.size() << " recurrences\n";
		}


    if( DemoteAllocasToHeap )
      demoteAllocasToHeap(fcn);

    splitBlocksAtRecurrences(recurrences);

    // For each fcn, we will create new basic blocks
    // called ENTRY, DESCEND, ASCEND and EXIT
    // We will create entry and descend now, since
    // we need them to raise arguments to locals.
    // We will create ascend and exit later.
    BasicBlock *bb_entry = BasicBlock::Create(ctx, "Entry", &fcn);
    BasicBlock *bb_descend = BasicBlock::Create(ctx, "Descend", &fcn);

    // Make our new block 'entry' the new entry block.
    BasicBlock *oldEntry = & fcn.getEntryBlock();
    bb_entry->moveBefore(oldEntry);
    bb_descend->moveAfter(bb_entry);

    // Entry branches to descend, and descend to oldEntry
    Instruction *t_entry = BranchInst::Create(bb_descend, bb_entry);
    Instruction *t_descend = BranchInst::Create(oldEntry, bb_descend);

    // determine which arguments are induction variables
    findInductionVariables(fcn, recurrences);

    // We want to put all function arguments into local variables.
    // This way, they will be saved/restored like every other
    // live value, and do not (necessarily) need space in the
    // activation record.
    typedef DenseMap<Argument*,PHINode*> Arg2PHI;
    Arg2PHI arg2phi;
    for(ArgIt i=fcn.arg_begin(), e=fcn.arg_end(); i!=e; ++i)
    {
      Argument *arg = &*i;
      PHINode *phi = PHINode::Create(arg->getType(), 0,
        Twine("Arg_") + arg->getName(),
        t_descend );

      arg->replaceAllUsesWith(phi);
      arg2phi[arg] = phi;

      phi->addIncoming(arg, bb_entry);
    }

    // determine which values are live
    // across each callsite.
    //  Some of these will be saved to
    //    the activation record.
    //  Others will be recomputed after
    //    a return.
    Callsite2ValueList liveValuesPerCallsite;
    findLiveValuesPerCallsite(fcn, recurrences, liveValuesPerCallsite);

    // determine which values could be rematerialized after a callsite,
    // and which must be saved across each callsite.
    Callsite2ValueList rematPerCallsite, stackSavePerCallsite;
    findRematOpportunities(fcn, recurrences, liveValuesPerCallsite,
      rematPerCallsite, stackSavePerCallsite);

    // build the frame type, and the LiveValues table to organize it.
    LiveValueInfoTable liveValues;

    // Create a entry for every live value
    // Count the number of callsites which must save each
    unsigned cs_no = 0;
    for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i, ++cs_no)
    {
      Value *callInst = i->getInstruction();

      ValueList &all = liveValuesPerCallsite[ callInst ];
      for(VLI j=all.begin(), f=all.end(); j!=f; ++j)
        liveValues[*j]; // create the record

      // Count the number of callsites which use each
      ValueList &stackSave = stackSavePerCallsite[ callInst ];
      for(VLI j=stackSave.begin(), f=stackSave.end(); j!=f; ++j)
      {
        LiveValueInfo &lvi = liveValues[*j];
        lvi.callsites.resize( recurrences.size() );
        lvi.callsites.set(cs_no);
      }
    }

    StructType *framety = makeFrameTy(
      fcn, recurrences, stackSavePerCallsite, liveValues);
    PointerType *ptr_framety = PointerType::getUnqual(framety);

//    TargetData &td = getAnalysis<TargetData>();
		const DataLayout &td = fcn.getParent()->getDataLayout();
    const unsigned frameSizeBytes = td.getTypeSizeInBits(framety)/8;

    if ( debug ) { 
      errs() << "\t- Activation record is "
             << frameSizeBytes << " bytes\n";
		}

    BasicBlock *bb_ascend = BasicBlock::Create(ctx, "Ascend", &fcn);
    BasicBlock *bb_exit = BasicBlock::Create(ctx, "Exit", &fcn);
    bb_exit->moveAfter( &fcn.back() );
    bb_ascend->moveBefore( bb_exit );

    // We will use one alloca instruction in the entry to store
    // the 'current' activation record; this avoids all sorts
    // of PHI confusion.
    AllocaInst *activation_record_storage = new AllocaInst(
      ptr_framety, td.getAllocaAddrSpace(), one, "Frame", t_entry);
    new StoreInst(ConstantPointerNull::get(ptr_framety),
      activation_record_storage, t_entry);

    PointerType *fpty = cast<PointerType>( fcn.getType() );
    FunctionType *fty = cast<FunctionType>(fpty->getElementType());
    Type *retty = fty->getReturnType();
    const bool hasReturnValue = ! retty->isVoidTy();
    Value *retval_alloca = 0;

    // In the entry block, we will create:
    //  (1) An alloca slot for each live value that spans a call
    //      or which will be rematerialized.
    //      Effectively, we are demoting reg to mem so that we
    //      don't have to worry about placing PHIs in the right place.
    //      (which should be removed by -mem2reg)
    //  (2) An alloca slot for the return value.
    //      Again, we demoting reg to mem to make PHIs easier.
    //      (which should be removed by -mem2reg)
    {
      // Create an alloca for each of the live values
      for(LVIT i=liveValues.begin(), e=liveValues.end(); i!=e; ++i)
      {
        const Value *value = i->first;
        LiveValueInfo &lvi = i->second;
        lvi.stackSlot = new AllocaInst(value->getType(), td.getAllocaAddrSpace() ,one,
          Twine("LV_")+value->getName(),t_entry);
      }

      // Create an alloca for the return value
      if( hasReturnValue )
        retval_alloca = new AllocaInst(retty, td.getAllocaAddrSpace(),
																				one,"ReturnValue",t_entry);
    }

    // Descend will:
    //  (0) save the stack
    //  (1) create a new activation record,
    //      populating the 'prev' and 'retspec' fields.
    PHINode *retspec = 0;
    {
      // create a new activation record.
      Value *activation1 = 0;

      if( AllocateFramesOnHeap )
      {
        // Create it on the heap.
        Value *malloc = getMalloc(*mod);
        Value *call = CallInst::Create(
          malloc, ConstantInt::get(int64ty, frameSizeBytes),
          "", t_descend);
        activation1 = new BitCastInst(
          call, ptr_framety, "NewFrame", t_descend);
      }
      else
      {
        // Create it on the stack.
        Type *byteptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
        Constant *StackSave = mod->getOrInsertFunction(
          "llvm.stacksave", byteptr);
        // Save stack in descend
        Value *restoreptr = CallInst::Create(
          StackSave, "OldSP", t_descend );

        activation1 = new AllocaInst(
          framety, td.getAllocaAddrSpace(), one,"NewFrame", t_descend);

        // store old sp into the frame
        Value *indices[2];
        indices[0] = zero;
        indices[1] = two;
        if( !multipleRecurrences )
          indices[1] = one;

        ArrayRef<Value *> indicesRef(indices, indices + 2);

        Value *gep = GetElementPtrInst::CreateInBounds(
          activation1, indicesRef, "NewFrame.OldSP", t_descend);
        new StoreInst(restoreptr, gep, t_descend);
      }
      // The 'next activation record' field
      {
        // Load the old activation record.
        Value *old_frame = new LoadInst(
          activation_record_storage, "OldFrame", t_descend );

        Value *indices[2];
        indices[0] = indices[1] = zero;

        ArrayRef<Value *> indicesRef(indices, indices + 2);

        Value *gep = GetElementPtrInst::CreateInBounds(
          activation1, indicesRef, "NewFrame.Next", t_descend);
        new StoreInst(old_frame, gep, t_descend);
      }
      // The 'return specifier' field
      if( multipleRecurrences )
      {
        retspec = PHINode::Create(intty, 0, "RetSpec",&bb_descend->front());
        retspec->addIncoming(ConstantInt::get(intty,~1UL,false),bb_entry);

        Value *indices[2];
        indices[0] = zero;
        indices[1] = one;

        ArrayRef<Value *> indicesRef(indices, indices + 2);

        Value *gep = GetElementPtrInst::CreateInBounds(
          activation1, indicesRef,
          "NewFrame.RetSpec", t_descend);
        new StoreInst(retspec, gep, t_descend);
      }

      // Save the new activation  record.
      new StoreInst( activation1, activation_record_storage,
        t_descend);
    }

    // Ascend will:
    // pop an activation record, and
    // branch according to the 'return specifier' field.
    SwitchInst *switch_return = 0;
    {
      // All exits will branch to the ascend block.
      for(FunctionExits::iterator i=exits.begin(), e=exits.end(); i!=e; ++i)
      {
        ReturnInst *ret = *i;
        BasicBlock *retblock = ret->getParent();

        if( hasReturnValue )
          new StoreInst(ret->getReturnValue(), retval_alloca, retblock);
        BranchInst::Create( bb_ascend, retblock );

        // Replace the return instruction with a branch to ascend.
        ret->eraseFromParent();
      }

      LoadInst *newFrame = new LoadInst(
        activation_record_storage, "TopFrame", bb_ascend );
      Value *indices[2];
      indices[0] = indices[1] = zero;

      ArrayRef<Value *> indicesRef(indices, indices + 2);

      Value *gep = GetElementPtrInst::CreateInBounds(
        newFrame, indicesRef,
        "TopFrame.Next", bb_ascend);
      LoadInst *next_frame = new LoadInst(
        gep, "PopFrame", bb_ascend);
      new StoreInst(next_frame, activation_record_storage, bb_ascend);
      indices[1] = one;

      Value *ret_spec = 0;
      // load the return specifier BEFORE stack restore!!!
      if( multipleRecurrences )
      {
        gep = GetElementPtrInst::CreateInBounds(
          newFrame, indicesRef,
          "TopFrame.RetSpec", bb_ascend);
        ret_spec = new LoadInst(
          gep, "ReturnSpecifier", bb_ascend);
      }

      if( AllocateFramesOnHeap )
      {
        // free() the frame
        Value *free = getFree(*mod);
        Type *byteptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
        Value *cast = new BitCastInst(
          newFrame, byteptr, "", bb_ascend);
        CallInst::Create(free, cast, "", bb_ascend);
      }
      else
      {
        // llvm.stackrestore() the frame.
        indices[0] = zero;
        indices[1] = two;
        if( !multipleRecurrences )
          indices[1] = one;

        Value *gep = GetElementPtrInst::CreateInBounds(
          newFrame, indicesRef,
          "TopFrame.OldSP", bb_ascend);
        LoadInst *restoreptr = new LoadInst(gep, "OldSP", bb_ascend);

        Type *byteptr = PointerType::getUnqual( Type::getInt8Ty(ctx) );
        Constant *StackRestore = mod->getOrInsertFunction(
          "llvm.stackrestore", Type::getVoidTy(ctx), byteptr);

        // Restore the stack frame
        CallInst::Create(StackRestore, restoreptr,"", bb_ascend );
      }

      if( multipleRecurrences )
      {
        switch_return = SwitchInst::Create(ret_spec, bb_exit,
          recurrences.size(), bb_ascend);
      }
      else
      {
        Value *lastFrame = CmpInst::Create(
          Instruction::ICmp,
          CmpInst::ICMP_EQ, next_frame,
          ConstantPointerNull::get(ptr_framety),
          "LastFrameTest", bb_ascend);

        switch_return = SwitchInst::Create(lastFrame, bb_exit,
          recurrences.size(), bb_ascend);
      }
    }

    // The exit block will return
    // (possibly a value)
    if( hasReturnValue )
    {
      LoadInst *load = new LoadInst(retval_alloca, "ReturnValue", bb_exit);
      ReturnInst::Create(ctx, load, bb_exit);
    }
    else
      ReturnInst::Create(ctx,bb_exit);

    // Each of the callsites will:
    //  (1) save live values.
    //  (2) compute the call's actual parameters
    //  (3) branch to descend.
    //  (4) return from ascend.
    //  (5) restore all saved live values.
    //  (6) rematerialize other live values.
    for(unsigned ri=0; ri<recurrences.size(); ++ri)
    {
      CallSite &cs = recurrences[ri];
      Instruction *callinst = cs.getInstruction();
			if ( debug )
	      errs() << "\t- Working on rec: " << *callinst << '\n';
      BasicBlock *after_recur_bb = callinst->getParent();

      // there must be a unique pred, because we split basic blocks
      BasicBlock *before_recur_bb = after_recur_bb->getUniquePredecessor();

      // Remove the old branch from before
      before_recur_bb->getTerminator()->eraseFromParent();

      // Load the current activation record.
      Value *thisFrame = new LoadInst(
        activation_record_storage, "ThisFrame", before_recur_bb );

      ValueList &ssHere = stackSavePerCallsite[callinst];
      ValueList &rematHere = rematPerCallsite[callinst];

			//////////////////////

			callinst->dump();
			errs() << "ssHere\n";
			for ( auto vi : ssHere )
				vi->dump();
			errs() << "rematHere\n";
			for ( auto vi : rematHere )
				vi->dump();
			errs() << "\n";

			//////////////////////

      // Caller will save all live values into the OLD frame.
      for(VLI i=ssHere.begin(), e=ssHere.end(); i!=e; ++i)
      {
        const Value *lv = *i;
        LiveValueInfo &lvi = liveValues[lv];

        LoadInst *load = new LoadInst(
          lvi.stackSlot, Twine("CallerSave_") + lv->getName(),
          before_recur_bb);

        Value *indices[2];
        indices[0] = zero;
        indices[1] = ConstantInt::get(intty, lvi.ar_index);
        ArrayRef<Value *> indicesRef(indices, indices + 2);
        Value *gep = GetElementPtrInst::CreateInBounds(
          thisFrame, indicesRef,
          "ThisFrame." + lv->getName(), before_recur_bb);

        new StoreInst(load, gep, before_recur_bb);
      }

      // The 'return specifier' field
      if( multipleRecurrences )
      {
        ConstantInt *ret_spec = cast<ConstantInt>(
          ConstantInt::get(intty,ri,false) );
        retspec->addIncoming( ret_spec, before_recur_bb );

        // Ascend will branch to this block.
        switch_return->addCase(ret_spec, after_recur_bb);
      }
      else
      {
        // Ascend will branch to this block.
        switch_return->addCase(
          ConstantInt::getFalse(ctx), after_recur_bb);
      }

      // Each of the actual parameters
      unsigned opi=0;
      for(ArgIt i=fcn.arg_begin(), e=fcn.arg_end(); i!=e; ++i, ++opi)
      {
        Argument *arg = &*i;
        PHINode *phi = arg2phi[arg];
        phi->addIncoming( cs.getArgument(opi), before_recur_bb);
      }

      // This block now branches to descend.
      BranchInst::Create(bb_descend, before_recur_bb);


      Instruction *where = &after_recur_bb->front();

      // Caller will restore the saved live values from the old frame.
      thisFrame = new LoadInst(
        activation_record_storage, "ThisFrame", where);
      for(VLI i=ssHere.begin(), e=ssHere.end(); i!=e; ++i)
      {
        const Value *lv = *i;
        LiveValueInfo &lvi = liveValues[lv];

        Value *indices[2];
        indices[0] = zero;
        indices[1] = ConstantInt::get(intty, lvi.ar_index);
        ArrayRef<Value *> indicesRef(indices, indices + 2);
        Value *gep = GetElementPtrInst::CreateInBounds(
          thisFrame, indicesRef,
          "ThisFrame." + lv->getName(), where);

        LoadInst *load = new LoadInst(
          gep,
          Twine("CallerRestore_")+lv->getName(),
          where );

        new StoreInst(load, lvi.stackSlot, where );
      }

      // Caller will rematerialize other live values.
      for(VLI i=rematHere.begin(), e=rematHere.end(); i!=e; ++i)
      {
        const Value *lv = *i;
        LiveValueInfo &lvi = liveValues[lv];

        const Instruction *old = cast<Instruction>( lv );

        Instruction *clone = old->clone();
        clone->setName( Twine("Remat_") + old->getName() );
        clone->insertBefore( where );

        // Make sure all operands are available.
        // In particular, this means that
        //  if any of the operands are in the liveValue table,
        //  then we must load them from a stack-slot.
        for(Instruction::op_iterator j=clone->op_begin(), f=clone->op_end(); j!=f; ++j)
        {
          Instruction *operand = dyn_cast<Instruction>(*j);
          if( !operand )
            continue;

          if( !liveValues.count(operand) )
            continue;

          LoadInst *load_op = new LoadInst(
            liveValues[operand].stackSlot,
            Twine("RematOperand"),
            clone);

          clone->replaceUsesOfWith(operand, load_op);
        }

        new StoreInst(clone, lvi.stackSlot, where );
      }


      // Caller will load the return value HERE
      if( hasReturnValue )
      {
        Value *returnedvalue = new LoadInst(
          retval_alloca, "ReturnValue", where);
        if( liveValues.count(callinst) )
          new StoreInst(
            returnedvalue, liveValues[callinst].stackSlot, where);
        else
          callinst->replaceAllUsesWith(returnedvalue);
      }
    }

		errs() << "\n";
    // Raise live values to allocas.
    // Replace all uses of every live value
    // with a load from it's storage location
    for(LVIT i=liveValues.begin(), e=liveValues.end(); i!=e; ++i)
    {
      const Value *value = i->first;
      LiveValueInfo &lvi = i->second;

			errs() << "\n" << *value << "\n";
      // We are going to modify the use list, so we
      // must make a temporary copy.
			
			std::set<Instruction *> userSet;
			userSet.clear();
			for ( auto use : value->users() )
				if ( Instruction *useInst = 
						dyn_cast<Instruction>( const_cast<User *>(use) ) )
					userSet.insert(useInst);

//      ValueList usestmp(value->use_begin(), value->use_end());
//      for(VLI j=usestmp.begin(), f=usestmp.end(); j!=f; ++j)
			for ( auto user : userSet )
      {
//        Value *use = const_cast<Value*>( *j );
//				User *user = const_cast<User *>( *ui );
        errs() << "====> " << *user << '\n';

        Instruction *iuse = dyn_cast<Instruction>(user);
        if( iuse )
        {
          // Load immediately before the use
          // Unless that use is a PHI node.
          PHINode *phi = dyn_cast<PHINode>(iuse);
          if( phi )
          {
            // At the tail of every predecessor of this phi
            for(unsigned pred=0; pred<phi->getNumIncomingValues(); ++pred)
            {
              if( phi->getIncomingValue(pred) == value )
              {
								if ( debug ) {
      	        	errs()
    	            << "\t- Raising use of live value "
  	              << lvi.stackSlot->getName()
	                << " to a stack load FOR A PHI NODE\n";
								}

                Instruction *term = phi->getIncomingBlock(pred)
                  ->getTerminator();

                LoadInst *load = new LoadInst(
                  lvi.stackSlot, value->getName(), term);
                phi->setIncomingValue(pred,load);

                // don't break; there may be more than one.
              }
            }
          }
          else
          {
						if ( debug ) {
	            errs()
  	            << "\t- Raising use of live value "
    	          << lvi.stackSlot->getName()
      	        << " to a stack load\n";
						}

            LoadInst *load = new LoadInst(
              lvi.stackSlot, value->getName(), iuse);
            // Replace the use
            iuse->replaceUsesOfWith(
              const_cast<Value*>( value ), load);
          }

        }
      }

      // Replace all defs of every live value with
      // a store to it's storage location.
      // It's SSA, so there is only one def!
      Instruction *ival = const_cast< Instruction* >(
        dyn_cast<Instruction>( value ) );
      if( ival )
      {
        // The return value of a callsite should not count
        // as a def, since we are replacing the return value
        // mechanism.  Here, we test for that case directly,
        // which is faster than searching the recurrences vector.
        bool isReturnValueOfACallsite = false;
        if( isa<CallInst>(ival) || isa<InvokeInst>(ival) )
        {
          CallSite cs(ival);

          if( cs.getCalledFunction() == &fcn )
            isReturnValueOfACallsite = true;
        }

        if( !isReturnValueOfACallsite )
        {
//          DEBUG(errs()
//            << "\t- Raising def of live value "
//            << lvi.value->getName()
//            << " to a stack store\n");
          Instruction *next = ival;
          do
            next = &*(++next->getIterator());
          while( isa<PHINode>( next ) );

          new StoreInst(ival, lvi.stackSlot, next);
        }
      }
    }

    // Remove all of the old call instructions.
    for(RecIt i=recurrences.begin(), e=recurrences.end(); i!=e; ++i)
      i->getInstruction()->eraseFromParent();


    // TODO: optimize for tail recursion
    (void)tailRecursive;

//    errs() << fcn;
    numCallsites += recurrences.size();
    ++numFcns;
		if ( debug )
	    errs() << "Rec2Iter exits " << fcn.getName() << ".\n";

		if ( debug ) {
			fcn.dump();
		
		}

    return true;
  }
}
