#define DEBUG_TYPE      "loopaa"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "corelab/Utilities/CallSiteFactory.h"
#include "corelab/Utilities/GetMemOper.h"
#include "corelab/Analysis/LoopAA.h"
#include "corelab/Utilities/GetDataLayout.h"

#include <cstdio>

namespace corelab
{
  using namespace llvm;

  char LoopAA::ID = 0;
  char NoLoopAA::ID = 0;
  char AAToLoopAA::ID = 0;
  char EvalLoopAA::ID = 0;

  cl::opt<bool> FULL_UNIVERSAL("full-universal",
                               cl::init(true), cl::Hidden,
                               cl::desc("Assume full visibility"));

  namespace
  {
    RegisterAnalysisGroup< LoopAA > loopaa("Loop-sensitive Alias Analysis");

    static RegisterPass<NoLoopAA>
    A("no-loop-aa",
      "No loop alias analysis (always return may-alias)",
      true, true);

    static RegisterAnalysisGroup<LoopAA, true> X(A);

    // Default: -basic-loop-aa
    static RegisterPass<AAToLoopAA>
    B("aa-to-loop-aa",
      "Basic loop AA (chain's to llvm::AliasAnalysis)",
      false, true);

    static RegisterAnalysisGroup<LoopAA> Y(B);

    static RegisterPass<EvalLoopAA> C("eval-loop-aa",
      "Exhaustive evaluation of loop AA", false, false);
  }


//------------------------------------------------------------------------
// Methods of the LoopAA interface

  LoopAA::LoopAA() : td(0), tli(0), nextAA(0), prevAA(0) {}

  LoopAA::~LoopAA()
  {
    if( nextAA )
      nextAA->prevAA = this->prevAA;
    if( prevAA )
      prevAA->nextAA = this->nextAA;

    getTopAA()->stackHasChanged();
  }

  void LoopAA::InitializeLoopAA(Pass *P)
  {
		const Module *M = getModuleFromVal((Value *)P);
    const DataLayout *t = &(M->getDataLayout());
    TargetLibraryInfo *ti = &P->getAnalysis< TargetLibraryInfoWrapperPass >().getTLI();
    LoopAA *naa = P->getAnalysis< LoopAA >().getTopAA();

    InitializeLoopAA(t,ti,naa);
//    InitializeLoopAA(ti,naa);

    getTopAA()->stackHasChanged();
  }

  void LoopAA::InitializeLoopAA(const DataLayout *t, TargetLibraryInfo *ti, LoopAA *naa)
  {
    td = t;
    tli = ti;

    // Don't insert this pass into the linked list twice
    if(prevAA || nextAA)
      return;

    // Insertion-sort this pass into the LoopAA stack.
    prevAA = 0;
    nextAA = naa;
    while( nextAA && nextAA->getSchedulingPreference() > this->getSchedulingPreference() )
    {
      prevAA = nextAA;
      nextAA = nextAA->nextAA;
    }

    if( prevAA )
      prevAA->nextAA = this;
    if( nextAA )
      nextAA->prevAA = this;

    assert(prevAA != this);
    assert(nextAA != this);

  }

  // find top of stack.
  LoopAA *LoopAA::getTopAA() const
  {
    // The stack is short, so this won't take long.
    LoopAA *top = const_cast<LoopAA *>(this);
    while( top->prevAA )
      top = top->prevAA;
    return top;
  }

  void LoopAA::getAnalysisUsage( AnalysisUsage &au ) const
  {
//    au.addRequired< TargetData >();
    au.addRequired< TargetLibraryInfoWrapperPass >();
    au.addRequired< LoopAA >(); // all chain.
  }

  const DataLayout *LoopAA::getTargetData() const
  {
    assert(td && "Did you forget to run InitializeLoopAA()?");
    return td;
  }

  const TargetLibraryInfo *LoopAA::getTargetLibraryInfo() const
  {
    assert(tli && "Did you forget to run InitializeLoopAA()?");
    return tli;
  }

  LoopAA::SchedulingPreference LoopAA::getSchedulingPreference() const
  {
    return Normal;
  }

  LoopAA::TemporalRelation LoopAA::Rev(TemporalRelation a)
  {
    switch(a)
    {
      case Before:
        return After;

      case After:
        return Before;

      case Same:
      default:
        return Same;
    }
  }

  LoopAA::AliasResult LoopAA::alias(
    const Value *ptrA, unsigned sizeA,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->alias(ptrA,sizeA,rel,ptrB,sizeB,L);
  }


  LoopAA::ModRefResult LoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->modref(A,rel,ptrB,sizeB,L);
  }


  LoopAA::ModRefResult LoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L)
  {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->modref(A,rel,B,L);
  }

  bool LoopAA::pointsToConstantMemory(const Value *P, const Loop *L) {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->pointsToConstantMemory(P, L);
  }

  bool LoopAA::canBasicBlockModify(const BasicBlock &BB,
                                   TemporalRelation Rel,
                                   const Value *Ptr,
                                   unsigned Size,
                                   const Loop *L) {
    return canInstructionRangeModify(BB.front(), Rel, BB.back(), Ptr, Size, L);
  }

  bool LoopAA::canInstructionRangeModify(const Instruction &I1,
                                         TemporalRelation Rel,
                                         const Instruction &I2,
                                         const Value *Ptr,
                                         unsigned Size,
                                         const Loop *L) {
    assert(I1.getParent() == I2.getParent() &&
           "Instructions not in same basic block!");
    BasicBlock::const_iterator I = I1.getIterator();
    BasicBlock::const_iterator E = I2.getIterator();
    ++E;  // Convert from inclusive to exclusive range.

    for (; I != E; ++I) // Check every instruction in range
      if (modref(&*I, Rel, Ptr, Size, L) & Mod)
        return true;
    return false;
  }

  bool LoopAA::mayModInterIteration(const Instruction *A,
                                    const Instruction *B,
                                    const Loop *L) {

    if(A->mayWriteToMemory()) {
      ModRefResult a2b = modref(A, Before, B, L);
      if(a2b & LoopAA::Mod)
        return true;
    }

    if(B->mayWriteToMemory()) {
      ModRefResult b2a = modref(B, After, A, L);
      if(b2a & Mod)
        return true;
    }

    return false;
  }

  void LoopAA::dump() const
  {
    print(errs());
  }

  void LoopAA::print(raw_ostream &out) const
  {
    out << "LoopAA Stack, top to bottom:\n";

    for(const LoopAA *i=this; i!=0; i=i->nextAA)
    {
      out << "\to " << i->getLoopAAName() << '\n';
    }
  }

  static const Function *getParent(const Value *V) {
    if (const Instruction *inst = dyn_cast<Instruction>(V))
      return inst->getParent()->getParent();

    if (const Argument *arg = dyn_cast<Argument>(V))
      return arg->getParent();

    return NULL;
  }

  bool LoopAA::isInterprocedural(const Value *O1, const Value *O2) {

    const Function *F1 = getParent(O1);
    const Function *F2 = getParent(O2);

    return F1 && F2 && F1 != F2;
  }

  void LoopAA::stackHasChanged()
  {
    uponStackChange();

    if( nextAA )
      nextAA->stackHasChanged();
  }

  void LoopAA::uponStackChange() {}

//------------------------------------------------------------------------
// Methods of NoLoopAA

  NoLoopAA::NoLoopAA() : LoopAA(), ImmutablePass(ID) {}

  void NoLoopAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    au.setPreservesAll();
  }

  LoopAA::SchedulingPreference NoLoopAA::getSchedulingPreference() const
  {
    return SchedulingPreference(Bottom-1);
  }

  LoopAA::AliasResult NoLoopAA::alias(
    const Value *ptrA, unsigned sizeA,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    errs() << "NoLoopAA\n";
    return MayAlias;
  }


  LoopAA::ModRefResult NoLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    errs() << "NoLoopAA\n";
    if( ! A->mayReadOrWriteMemory() )
      return NoModRef;
    else if( ! A->mayReadFromMemory() )
      return Mod;
    else if( ! A->mayWriteToMemory() )
      return Ref;
    else
      return ModRef;
  }


  LoopAA::ModRefResult NoLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L)
  {
    errs() << "NoLoopAA\n";

    if( ! A->mayReadOrWriteMemory() || ! B->mayReadOrWriteMemory() )
      return NoModRef;
    else if( ! A->mayReadFromMemory() )
      return Mod;
    else if( ! A->mayWriteToMemory() )
      return Ref;
    else
      return ModRef;
  }

  bool NoLoopAA::pointsToConstantMemory(const Value *P, const Loop *L) {
    return false;
  }

//------------------------------------------------------------------------
// Methods of AAToLoopAA

  /// Conservatively raise an llvm::AliasAnalysis::AliasResult
  /// to a liberty::LoopAA::AliasResult.
  AAToLoopAA::AliasResult AAToLoopAA::Raise(llvm::AliasResult ar)
  {
    switch(ar)
    {
      case llvm::NoAlias:
        return LoopAA::NoAlias;

      case llvm::MustAlias:
        return LoopAA::MustAlias;

      case llvm::MayAlias:
      default:
        return LoopAA::MayAlias;
    }
  }

  /// Conservatively raise an llvm::AliasAnalysis::ModRefResult
  /// to a liberty::LoopAA::ModRefResult.
  AAToLoopAA::ModRefResult AAToLoopAA::Raise(llvm::ModRefInfo mr)
  {
    switch(mr)
    {
			case llvm::ModRefInfo::NoModRef:
        return NoModRef;
      case llvm::ModRefInfo::Ref:
        return Ref;
      case llvm::ModRefInfo::Mod:
        return Mod;
      case llvm::ModRefInfo::ModRef:
      default:
        return ModRef;
    }
  }


  AAToLoopAA::AAToLoopAA() : FunctionPass(ID), LoopAA(), AA(0) {}

  /// Determine if L contains I, and no subloops of L contain I.
  static bool isInnermostContainingLoop(const Loop *L, const Instruction *I)
  {
    const BasicBlock *p = I->getParent();

    if( !L->contains(p) )
      return false;

    for(Loop::iterator i=L->begin(), e=L->end(); i!=e; ++i)
    {
      Loop *subloop = *i;
      if( subloop->contains(p) )
        return false;
    }

    return true;
  }

  /// Determine if llvm::AliasAnalysis is valid for this query.
  static bool isValid(const Loop *L, const Instruction *I)
  {
    return !L || isInnermostContainingLoop(L,I);
  }

  static bool isValid(const Loop *L, const Value *P) {

    if(const Instruction *I = dyn_cast<Instruction>(P))
      return isValid(L, I);

    return true;
  }

  void AAToLoopAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    LoopAA::getAnalysisUsage(au);
    au.addRequired< AAResultsWrapperPass >();
    au.setPreservesAll();
  }

  bool AAToLoopAA::runOnFunction(Function &fcn)
  {
    InitializeLoopAA(this);
    AA = &getAnalysis< AAResultsWrapperPass >().getAAResults();
    return false;
  }

  LoopAA::AliasResult AAToLoopAA::alias(
    const Value *ptrA, unsigned sizeA,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    (errs() << "AAToLoopAA\n");
    if( rel == Same && isValid(L, ptrA) && isValid(L, ptrB) )
    {
      AliasResult r = Raise( AA->alias(ptrA,sizeA, ptrB,sizeB) );
      if( r != MayAlias )
        return r;
    }

    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L);
  }

  LoopAA::ModRefResult AAToLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    (errs() << "AAToLoopAA\n");
    // llvm::AliasAnalysis is only valid for innermost
    // loop!
    if( rel == Same && isValid(L,A) && isValid(L,ptrB) )
    {
      ModRefResult r = Raise( AA->getModRefInfo(A,ptrB,sizeB) );
      if( r == NoModRef )
        return r;
      else
        return ModRefResult( r & LoopAA::modref(A,rel,ptrB,sizeB,L) );
    }

    // Couldn't say anything specific; chain to lower analyses.
    return LoopAA::modref(A,rel,ptrB,sizeB,L);
  }

  LoopAA::ModRefResult AAToLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L)
  {
    // Why do we sometimes reverse queries:
    //
    // Two situations:
    //    load/store vs call
    //    intrinsic vs intrinsic
    //
    // llvm::AliasAnalysis is very asymmetric.
    // You always get better results with call vs load/store than load/store vs call.

    (errs() << "AAToLoopAA\n");
    // llvm::AliasAnalysis is only valid for innermost
    // loop!
    if( rel == Same )
    {
      if( isValid(L,A) || isValid(L,B) )
      {
        CallSite csA = getCallSite(const_cast<Instruction*>(A));
        CallSite csB = getCallSite(const_cast<Instruction*>(B));

        if( csA.getInstruction() && csB.getInstruction() )
        {
          ModRefResult r = Raise( AA->getModRefInfo(ImmutableCallSite(A),ImmutableCallSite(B)) );
          if( r == NoModRef )
            return NoModRef;

          else if( isa<IntrinsicInst>(A)
          &&       isa<IntrinsicInst>(B)
          &&       AA->getModRefInfo(ImmutableCallSite(B),ImmutableCallSite(A)) == llvm::ModRefInfo::NoModRef )
            // Conservatively reverse the query (see note at top of fcn)
            return NoModRef;

          else
            return ModRefResult( r & LoopAA::modref(A,rel,B,L) );
        }
        else if( csB.getInstruction() )
        {
          const Value *ptrA = getMemOper(A);
          PointerType *pty = cast<PointerType>( ptrA->getType() );
          const unsigned sizeA = getTargetData()->getTypeSizeInBits( pty->getElementType() ) / 8;

          // Conservatively reverse the query (see note a t top of fcn)
          ModRefResult r = Raise( AA->getModRefInfo(B,ptrA,sizeA) );
          if( r == NoModRef )
            return r;

          else
            return LoopAA::modref(A,rel,B,L);

        }
        else if( const Value *ptrB = getMemOper(B) )
        {
          PointerType *pty = cast<PointerType>( ptrB->getType() );
          const unsigned sizeB = getTargetData()->getTypeSizeInBits( pty->getElementType() ) / 8;

          ModRefResult r = Raise( AA->getModRefInfo(A, ptrB,sizeB) );
          if( r == NoModRef )
            return r;
          else
            return ModRefResult( r & LoopAA::modref(A,rel,B,L) );
        }
      }
    }

    // Couldn't say anything specific; chain to lower analyses.
    return LoopAA::modref(A,rel,B,L);
  }

//------------------------------------------------------------------------
// Methods of EvalLoopAA

#undef DEBUG_TYPE
#define DEBUG_TYPE "evalloopaa"

  EvalLoopAA::EvalLoopAA()
    : FunctionPass(ID)
  {
    totals[0][0] = totals[0][1] = totals[0][2] = totals[0][3] = 0;
    totals[1][0] = totals[1][1] = totals[1][2] = totals[1][3] = 0;
  }

  static void printStats(const char *prefix, const char *prefix2, unsigned *array)
  {
      const unsigned no=array[0], mod=array[1], ref=array[2], modref=array[3];

      float sum = (no + mod + ref + modref)/100.;

      char buffer[100];
      snprintf(buffer,100, "%s %s No Mod/Ref: %5d    %3.1f\n",
        prefix, prefix2, no, no/sum);
      errs() << buffer;
      snprintf(buffer,100, "%s %s    Mod    : %5d    %3.1f\n",
        prefix, prefix2, mod, mod/sum);
      errs() << buffer;
      snprintf(buffer,100, "%s %s        Ref: %5d    %3.1f\n",
        prefix, prefix2, ref, ref/sum);
      errs() << buffer;
      snprintf(buffer,100, "%s %s    Mod+Ref: %5d    %3.1f\n",
        prefix, prefix2, modref, modref/sum);
      errs() << buffer;
  }

  EvalLoopAA::~EvalLoopAA()
  {
    printStats("Module", "INTRA", totals[0]);
    printStats("Module", "INTER", totals[1]);
  }

  void EvalLoopAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< LoopAA >();
    au.addRequired< LoopInfoWrapperPass >();
    au.setPreservesAll();
  }

  bool EvalLoopAA::runOnFunction(Function &fcn)
  {
    fcnTotals[0][0] = fcnTotals[0][1] = fcnTotals[0][2] = fcnTotals[0][3] = 0;
    fcnTotals[1][0] = fcnTotals[1][1] = fcnTotals[1][2] = fcnTotals[1][3] = 0;

    LoopInfo          &li = getAnalysis< LoopInfoWrapperPass >().getLoopInfo();

    std::vector<Loop*> loops( li.begin(), li.end() );
    while( ! loops.empty() ) {
      Loop *loop = loops.back();
      loops.pop_back();

      runOnLoop(loop);

      // append all sub-loops to the work queue
      loops.insert( loops.end(),
        loop->getSubLoops().begin(),
        loop->getSubLoops().end() );
    }

		errs() << "Results of LoopAA on function: "
            << fcn.getName() << ":\n";
    printStats(fcn.getName().str().c_str(), "INTRA", fcnTotals[0]);
    printStats(fcn.getName().str().c_str(), "INTER", fcnTotals[1]);
    return false;
  }

  bool EvalLoopAA::runOnLoop(Loop *L)
  {
    LoopAA *loopaa = getAnalysis< LoopAA >().getTopAA();

    loopTotals[0][0] = loopTotals[0][1] = loopTotals[0][2] = loopTotals[0][3] = 0;
    loopTotals[1][0] = loopTotals[1][1] = loopTotals[1][2] = loopTotals[1][3] = 0;

    // For every pair of instructions in this loop;
    for(Loop::block_iterator i=L->block_begin(), e=L->block_end(); i!=e; ++i)
    {
      const BasicBlock *bb = *i;
      for(BasicBlock::const_iterator j=bb->begin(), f=bb->end(); j!=f; ++j)
      {
        const Instruction *i1 = &*j;

        if( !i1->mayReadFromMemory() && !i1->mayWriteToMemory() )
          continue;

        for(Loop::block_iterator k=L->block_begin(); k!=e; ++k)
        {
          const BasicBlock *bb2 = *k;
          for(BasicBlock::const_iterator l=bb2->begin(), g=bb2->end(); l!=g; ++l)
          {
            const Instruction *i2 = &*l;

            if( !i2->mayReadFromMemory() && !i2->mayWriteToMemory() )
              continue;

            (errs() << "Query:\n\t" << *i1
                         <<       "\n\t" << *i2 << '\n');

            // don't ask reflexive, intra-iteration queries.
            if( i1 != i2 )
            {
              switch( loopaa->modref(i1,LoopAA::Same,i2,L) )
              {
                case LoopAA::NoModRef:
                  (errs() << "\tIntra: NoModRef\n");
                  ++loopTotals[0][0];
                  ++fcnTotals[0][0];
                  ++totals[0][0];
                  break;
                case LoopAA::Mod:
                  (errs() << "\tIntra: Mod\n");
                  ++loopTotals[0][1];
                  ++fcnTotals[0][1];
                  ++totals[0][1];
                  break;
                case LoopAA::Ref:
                  (errs() << "\tIntra: Ref\n");
                  ++loopTotals[0][2];
                  ++fcnTotals[0][2];
                  ++totals[0][2];
                  break;
                case LoopAA::ModRef:
                  (errs() << "\tIntra: ModRef\n");
                  ++loopTotals[0][3];
                  ++fcnTotals[0][3];
                  ++totals[0][3];
                  break;
              }
            }

            switch( loopaa->modref(i1,LoopAA::Before,i2,L) )
            {
              case LoopAA::NoModRef:
                (errs() << "\tInter: NoModRef\n");
                ++loopTotals[1][0];
                ++fcnTotals[1][0];
                ++totals[1][0];
                break;
              case LoopAA::Mod:
                (errs() << "\tInter: Mod\n");
                ++loopTotals[1][1];
                ++fcnTotals[1][1];
                ++totals[1][1];
                break;
              case LoopAA::Ref:
                (errs() << "\tInter: Ref\n");
                ++loopTotals[1][2];
                ++fcnTotals[1][2];
                ++totals[1][2];
                break;
              case LoopAA::ModRef:
                (errs() << "\tInter: ModRef\n");
                ++loopTotals[1][3];
                ++fcnTotals[1][3];
                ++totals[1][3];
                break;
            }
          }
        }

      }
    }

    BasicBlock *header = L->getHeader();
    const char *loopName = header->getName().str().c_str();
    errs() << "Results of LoopAA on loop: "
           << header->getParent()->getName()
           << "::" << header->getName() << ":\n";

    printStats(loopName, "INTRA", loopTotals[0]);
    printStats(loopName, "INTER", loopTotals[1]);
    return false;
  }
}

