#define DEBUG_TYPE "global-malloc-aa"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "corelab/Analysis/ClassicLoopAA.h"
#include "corelab/Analysis/FindSource.h"
#include "corelab/Analysis/LoopAA.h"
#include "corelab/Utilities/CaptureUtil.h"
#include "corelab/Utilities/GetDataLayout.h"


using namespace llvm;
using namespace corelab;

class GlobalMallocAA : public ModulePass, public corelab::ClassicLoopAA {

private:
  typedef Module::const_global_iterator GlobalIt;
  typedef Value::const_use_iterator UseIt;

  typedef DenseSet<const Value *> ValueSet;
  typedef SmallPtrSet<const Instruction *, 1> CISet;
  typedef CISet::const_iterator CISetIt;
  typedef DenseMap<const Value *, CISet> VToCIMap;

  ValueSet nonMalloc;
  ValueSet nonExclusive;
  VToCIMap mallocSrcs;

  static bool isExclusive(const GlobalValue *global,
                          const ValueSet &nonMalloc,
                          const CISet &sources) {

    if(nonMalloc.count(global))
      return false;

    if(corelab::findAllCaptures(global))
      return false;

    corelab::CaptureSet captureSet;

    for(CISetIt src = sources.begin(); src != sources.end(); ++src) {

      corelab::findAllCaptures(*src, &captureSet);
      assert(captureSet.size() != 0 && "How can a source not be captured!?");
      if(captureSet.size() > 1) {
        return false;
      }

      captureSet.clear();
    }

    for(UseIt use = global->use_begin(); use != global->use_end(); ++use) {
      if(isa<LoadInst>(*use) && corelab::findAllCaptures(*use)) {
        return false;
      }
    }

    (errs() << "Exclusive global: " << *global << "\n");

    return true;
  }

public:
  static char ID;
  GlobalMallocAA() : ModulePass(ID) {}

  virtual bool runOnModule(Module &M) {

    InitializeLoopAA(this);

    nonMalloc.clear();
    mallocSrcs.clear();

    for(GlobalIt global = M.global_begin(); global != M.global_end(); ++global) {
      Type *type = global->getType()->getElementType();
      if(type->isPointerTy()) {
        for(UseIt use = global->use_begin(); use != global->use_end(); ++use) {
          if(const StoreInst *store = dyn_cast<StoreInst>(*use)) {
            const Instruction *src = corelab::findNoAliasSource(store);
            if(src) {
              mallocSrcs[&*global].insert(src);
            } else {
              nonMalloc.insert(&*global);
            }
          } else if(!isa<LoadInst>(*use)) {
            nonMalloc.insert(&*global);
          }
        }
      }

      if(!global->hasLocalLinkage() && !corelab::FULL_UNIVERSAL) {
        nonMalloc.insert(&*global);
      }
    }

    for(GlobalIt global = M.global_begin(); global != M.global_end(); ++global) {
      if(!isExclusive(&*global, nonMalloc, mallocSrcs[&*global])) {
        nonExclusive.insert(&*global);
      }
    }

    return false;
  }

  virtual AliasResult aliasCheck(const Pointer &P1,
                                 TemporalRelation Rel,
                                 const Pointer &P2,
                                 const Loop *L) {

    const Value *V1 = P1.ptr, *V2 = P2.ptr;

    const GlobalValue *V1GlobalSrc = corelab::findGlobalSource(V1);
    const GlobalValue *V2GlobalSrc = corelab::findGlobalSource(V2);

    const bool V1Exclusive = !nonExclusive.count(V1GlobalSrc);
    const bool V2Exclusive = !nonExclusive.count(V2GlobalSrc);

    if(V1GlobalSrc && V1Exclusive &&
       V1GlobalSrc != V2GlobalSrc &&
       !corelab::findLoadedNoCaptureArgument(V2))
      return NoAlias;

    if(V2GlobalSrc && V2Exclusive &&
       V2GlobalSrc != V1GlobalSrc &&
       !corelab::findLoadedNoCaptureArgument(V1))
      return NoAlias;

    if(V1GlobalSrc && V1Exclusive &&
       V2GlobalSrc && V2Exclusive &&
       V1GlobalSrc == V2GlobalSrc)
      return MayAlias;


    const bool V1MallocOnly = !nonMalloc.count(V1GlobalSrc);
    const bool V2MallocOnly = !nonMalloc.count(V2GlobalSrc);

    if(V1GlobalSrc && V1MallocOnly &&
       V2GlobalSrc && V2MallocOnly) {

      const CISet V1set = mallocSrcs[V1GlobalSrc];
      const CISet V2set = mallocSrcs[V2GlobalSrc];

      SmallPtrSet<const Instruction *, 4> both;
      for(CISetIt it = V1set.begin(); it != V1set.end(); ++it) {
        both.insert(*it);
      }

      bool noAlias = true;
      for(CISetIt it = V2set.begin(); it != V2set.end(); ++it) {
        if(!both.insert(*it).second) {
          noAlias = false;
        }
      }

      if(both.size() && noAlias) {
        return LoopAA::NoAlias;
      }
    }

    const Instruction *V1Src = corelab::findNoAliasSource(V1);
    const Instruction *V2Src = corelab::findNoAliasSource(V2);

    if(V1GlobalSrc && V2Src && V1MallocOnly &&
       !mallocSrcs[V1GlobalSrc].count(V2Src)) {
      return NoAlias;
    }

    if(V2GlobalSrc && V1Src && V2MallocOnly &&
       !mallocSrcs[V2GlobalSrc].count(V1Src)) {
      return NoAlias;
    }

		const Module *m = getModuleFromVal(V1);
		DataLayout dt = m->getDataLayout();

    const GlobalValue *V1Global =
      dyn_cast<GlobalValue>(GetUnderlyingObject(V1, dt));
    const GlobalValue *V2Global =
      dyn_cast<GlobalValue>(GetUnderlyingObject(V2, dt));

    if(V1GlobalSrc && V2Global && V1MallocOnly)
      return NoAlias;

    if(V2GlobalSrc && V1Global && V2MallocOnly)
      return NoAlias;

    return MayAlias;
  }

  const char *getLoopAAName() const {
    return "global-malloc-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.setPreservesAll();                         // Does not transform code
  }

  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }
};

char GlobalMallocAA::ID = 0;

static RegisterPass<GlobalMallocAA>
X("global-malloc-aa", "Alias analysis for globals pointers", false, true);
static RegisterAnalysisGroup<corelab::LoopAA> Y(X);

