#ifndef CLASSIC_LOOP_AA
#define CLASSIC_LOOP_AA

#include "corelab/Analysis/LoopAA.h"

namespace corelab {

  using namespace llvm;

  class ClassicLoopAA : public LoopAA {

  public:

    struct Pointer {
      const Instruction *inst;
      const Value *ptr;
      const unsigned size;

      Pointer(const Value *ptr,
              const unsigned size) : inst(NULL), ptr(ptr), size(size) {}

      Pointer(const Instruction *inst,
              const Value *ptr,
              const unsigned size) : inst(inst), ptr(ptr), size(size) {}
    };

  private:

    ModRefResult modrefSimple(const LoadInst *Load,
                              TemporalRelation Rel,
                              const Pointer &P,
                              const Loop *L);

    ModRefResult modrefSimple(const StoreInst *Store,
                              TemporalRelation Rel,
                              const Pointer &P,
                              const Loop *L);

    ModRefResult modrefSimple(const VAArgInst *VAArg,
                              TemporalRelation Rel,
                              const Pointer &P,
                              const Loop *L);

    ModRefResult modrefSimple(const Instruction *I,
                              TemporalRelation Rel,
                              const Pointer &P,
                              const Loop *L);

  public:

    /// May not call down the LoopAA stack, but may top
    virtual ModRefResult getModRefInfo(CallSite CS1,
                                       TemporalRelation Rel,
                                       CallSite CS2,
                                       const Loop *L);

    /// V is never a CallSite
    /// May not call down the LoopAA stack, but may top
    virtual ModRefResult getModRefInfo(CallSite CS,
                                       TemporalRelation Rel,
                                       const Pointer &P,
                                       const Loop *L);

    /// V1 is never a CallSite
    /// V2 is never a CallSite
    /// May not call down the LoopAA stack, but may top
    virtual AliasResult aliasCheck(const Pointer &P1,
                                   TemporalRelation Rel,
                                   const Pointer &P2,
                                   const Loop *L);

    virtual ModRefResult modref(const Instruction *I1,
                                TemporalRelation Rel,
                                const Instruction *I2,
                                const Loop *L);

    virtual ModRefResult modref(const Instruction *I,
                                TemporalRelation Rel,
                                const Value *V,
                                unsigned Size,
                                const Loop *L);

    virtual AliasResult alias(const Value *V1,
                              unsigned Size1,
                              TemporalRelation Rel,
                              const Value *V2,
                              unsigned Size2,
                              const Loop *L);
  };
}

#endif /* CLASSIC_LOOP_AA */
