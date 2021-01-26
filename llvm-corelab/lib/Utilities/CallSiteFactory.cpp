#include "corelab/Utilities/CallSiteFactory.h"

using namespace llvm;

CallSite corelab::getCallSite(Value *value) {
  return CallSite(value);
}

const CallSite corelab::getCallSite(const Value *value) {
  return getCallSite(const_cast<Value *>(value));
}

