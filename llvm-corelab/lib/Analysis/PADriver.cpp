#include <sstream>
#include <unistd.h>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>

#include "llvm/IR/Value.h"
#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"

#define DEBUG_TYPE "PADRIVER"

#include "corelab/Analysis/PADriver.h"
#include "corelab/Analysis/PointerAnalysis.h"

using namespace llvm;
using namespace corelab;

STATISTIC(PABaseCt, "Counts number of base constraints");
STATISTIC(PAAddrCt, "Counts number of address constraints");
STATISTIC(PALoadCt,  "Counts number of load constraints");
STATISTIC(PAStoreCt, "Counts number of store constraints");
STATISTIC(PANumVert, "Counts number of vertices");
STATISTIC(PAMerges,  "Counts number of merged vertices");
STATISTIC(PARemoves, "Counts number of calls to remove cycle");
STATISTIC(PAMemUsage, "kB of memory");

// ============================= //

PADriverTest::PADriverTest() : ModulePass(ID) {
    pointerAnalysis = new PointerAnalysisTest();
    currInd = 0;
    nextMemoryBlock = 1;

    PAAddrCt = 0;
    PABaseCt = 0;
    PALoadCt = 0;
    PAStoreCt = 0;
    PANumVert = 0;
    PARemoves = 0;
    PAMerges = 0;
    PAMemUsage = 0;

    numInst = 0;
}

bool PADriverTest::runOnModule(Module &M) {
	module = &M;

	if (pointerAnalysis == 0) pointerAnalysis = new PointerAnalysisTest();

	// Collect information from global variables
	for (Module::global_iterator git = M.global_begin(), gitE = M.global_end(); 
			git != gitE; ++git) {
		handleGlobalVariable(&*git);
	}

	// Collect information from functions
	for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
		if (!F->isDeclaration()) {
			addConstraints(*F);
			matchFormalWithActualParameters(*F);
			matchReturnValueWithReturnVariable(*F);
		}
    }

    pointerAnalysis->solve();

    // map memory regions back to values (assuming no structs)
		for ( auto i : memoryBlock ) {
        Value *v = i.first;
        std::vector<int> list = i.second;
        assert(list.size() == 1); // i.e. no structs
        int2mem[list[0]] = v;
    }

    double vmUsage, residentSet;
    process_mem_usage(vmUsage, residentSet);
    PAMemUsage = vmUsage;

    // Get some statistics
	PAMerges = pointerAnalysis->getNumOfMertgedVertices();
	PARemoves = pointerAnalysis->getNumCallsRemove();
	PANumVert = pointerAnalysis->getNumVertices();

	//XXX: Test for memory privatization
	checkMemoryPrivate();

	std::error_code EC;
	raw_fd_ostream printFile("PA.info", EC, llvm::sys::fs::OpenFlags::F_Text);
	print(printFile ,&M);

	return false;
}

Value *getOper(Instruction *v) {
	if ( isa<LoadInst>(v) )
		return v->getOperand(0);
	else
		return v->getOperand(1);
}

bool PADriverTest::isNoAlias(Value *v0, Value *v1) {
	std::set<int> mSet0 = pointerAnalysis->pointsTo(value2int[v0]);
	std::set<int> mSet1 = pointerAnalysis->pointsTo(value2int[v1]);
	if ( mSet0.size() == 0 || mSet1.size() == 0 )
		return false;

	for ( auto m0 : mSet0 )
		for ( auto m1 : mSet1 )
			if ( m0 == m1 )
				return false;
	return true;
}

std::set<int> PADriverTest::getPointsToSet(Value *v) {
	return pointerAnalysis->pointsTo(value2int[v]);
}


void PADriverTest::checkMemoryPrivate(void) {
	bool unResolvedPointer = false;

	//for load transformation
	for ( auto ii : value2int )
	{
		Value *pointerV = ii.first;
		int n = ii.second;

		std::set<int> pointsSet = pointerAnalysis->pointsTo(n);
		if ( pointsSet.size() == 0 ) {
			std::set<Value *> emptySet;
			emptySet.clear();
			pointer2Memory[pointerV] = emptySet;
		}
		else {
			for ( auto memoryId : pointsSet )
				(pointer2Memory[pointerV]).insert(int2mem[memoryId]);
		}
	}

	for ( auto i : memoryBlock ) {
		const Value *memoryV = i.first;
		const std::vector<int> list = i.second;
		int memoryInt = list[0];

		for ( auto ii : value2int ) {
			const Value *pointerV = ii.first;
			const int n = ii.second;
			std::set<int> pointsSet = pointerAnalysis->pointsTo(n);
			if ( pointsSet.size() == 0 &&
					isa<PointerType>(pointerV->getType()) &&
					//THIS IS HARDCODING PART
					!isa<ConstantPointerNull>(pointerV) &&
					!isa<Function>(pointerV)
					) {
//				errs() << "UNRESOLVED:\n";
//				pointerV->dump();
				unResolvedPointer = true;
			}
			if ( pointsSet.find(memoryInt) != pointsSet.end() )
				memory2Pointed[memoryInt].insert(n);
		}
	}
}

// ============================= //

// Get the int ID of a new memory space
int PADriverTest::getNewMem(std::string name) {

	// Get a new ID for the memory
	int n = ++currInd;
	nameMap[n] = name;

	return n;
}

// ============================= //

void PADriverTest::print(raw_fd_ostream &O, const Module *M) const {
    std::stringstream dotFileSS;
    pointerAnalysis->print(O);

    // print out value2Int mapping
    O << "Variable: Int -> Value* mapping\n";
		for ( auto i : value2int ) {
        const Value *v = i.first;
        const int n = i.second;
        O << n << ": " << *v << "\n";
    }

    O << "Memory: Int -> Value* mapping\n";
		for ( auto i : memoryBlock ) {
        const Value *v = i.first;
        const std::vector<int> list = i.second;

				//TODO: flexible handler
        assert(list.size() == 1); // i.e. no structs

        O << list[0] << ": " << *v << "\n";
    }


	pointerAnalysis->printDot(dotFileSS, M->getModuleIdentifier(), nameMap);
	O << dotFileSS.str();
}

// ============================= //

std::string PADriverTest::intToStr(int v) {
	std::stringstream ss;
	ss << v;
	return ss.str();
}

// ============================= //

void PADriverTest::process_mem_usage(double& vm_usage, double& resident_set) {
	using std::ios_base;
	using std::ifstream;
	using std::string;

	vm_usage     = 0.0;
	resident_set = 0.0;

	// 'file' stat seems to give the most reliable results
	//
	ifstream stat_stream("/proc/self/stat",ios_base::in);

	// dummy vars for leading entries in stat that we don't care about
	//
	string pid, comm, state, ppid, pgrp, session, tty_nr;
	string tpgid, flags, minflt, cminflt, majflt, cmajflt;
	string utime, stime, cutime, cstime, priority, nice;
	string O, itrealvalue, starttime;

	// the two fields we want
	//
	unsigned long vsize;
	long rss;

	stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr
		>> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt
		>> utime >> stime >> cutime >> cstime >> priority >> nice
		>> O >> itrealvalue >> starttime >> vsize >> rss; // don't care about the rest

	stat_stream.close();

	long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // in case x86-64 is configured to use 2MB pages
	vm_usage     = vsize / 1024.0;
	resident_set = rss * page_size_kb;
}

// ============================= //

void PADriverTest::findAllPointerOperands(User *U, std::set<Value *> &ptrs) {
    for (unsigned i = 0; i < U->getNumOperands(); i++) {
        Value *srcV = U->getOperand(i);
        User *src = dyn_cast<User>(srcV);
        if (!src)
            continue;
        const Type *srcTy = src->getType();
        if (srcTy->isPointerTy() ||
            // this is the case where we have an operand
            // that we know has a pointer assigned to it, like the trunc:
            //      %4 = add i64 %3, zext (i32 ptrtoint ([24 x i32]* @tqmf to
            //      i32) to i64)
            //      %5 = trunc i64 %4 to i32
            value2int.count(src)) {
            // errs() << "Found pointer: " << *src << "\n";
            ptrs.insert(src);
        }

        ConstantExpr *C = dyn_cast<ConstantExpr>(src);
        if (C) {
            // errs() << "Found constant: " << *C << "\n";
            findAllPointerOperands(src, ptrs);
        }
    }
}

void PADriverTest::handleOperator(Value *v) {
	assert( isa<Operator>(v) );
	User *user = dyn_cast<User>(v);
	assert(user);

	if ( isa<BitCastOperator>(v) ) {
		Value *src = user->getOperand(0);
		Value *dst = v;

		int a = Value2Int(dst);
		int b = Value2Int(src);
		pointerAnalysis->addBase(a, b);
	}
	else if ( isa<GEPOperator>(v) ) {
		Value *pointerV = dyn_cast<GEPOperator>(v)->getPointerOperand();
		
		int a = Value2Int(v);
		int b = Value2Int(pointerV);
		pointerAnalysis->addBase(a, b);
	}
	else if ( isa<PtrToIntOperator>(v) ) { 
		Value *src = user->getOperand(0);
		Value *dst = v;

		int a = Value2Int(dst);
		int b = Value2Int(src);
		pointerAnalysis->addBase(a, b);
	}
	else if ( isa<ZExtOperator>(v) ) { // not related to pointer

	}
}

void PADriverTest::addConstraints(Function &F) {
    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
            if (isa<PHINode>(I)) {
				PHINode *Phi = dyn_cast<PHINode>(I);
				const Type *Ty = Phi->getType();

				if (Ty->isPointerTy()) {
					unsigned n = Phi->getNumIncomingValues();
					std::vector<Value*> values;

					for (unsigned i = 0; i < n; i++) {
						Value *v = Phi->getIncomingValue(i);

						values.push_back(v);
					}

					phiValues[&*I] = values;
				}
			}
		}
	}

	for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
		for (BasicBlock::iterator I_ = BB->begin(), E = BB->end(); I_ != E; ++I_) {

			numInst++;
			Instruction *I = &*I_;

			if (isa<CallInst>(I)) {
				CallInst *CI = dyn_cast<CallInst>(I);

				if (CI) {
					Function *FF = CI->getCalledFunction();

					if (FF && (FF->getName() == "malloc" || FF->getName() == "realloc" ||
								FF->getName() == "calloc")) {
						std::vector<int> mems;

						if (!memoryBlock.count(I)) {
							mems.push_back(getNewMemoryBlock());
							memoryBlock[I] = mems;
						} else {
							mems = memoryBlock[I];
						}

						int a = Value2Int(I);
						pointerAnalysis->addAddr(a, mems[0]);
						PAAddrCt++;
					}
				}
			}

            // Handle special operations
            switch (I->getOpcode()) {
            case Instruction::Select: {

                Type *Ty = I->getType();
                if (Ty->isPointerTy()) {
                    // %.data.i.i = select i1 %24, i8* %21, i8* %1
                    int a = Value2Int(I);

                    int b = Value2Int(I->getOperand(1));
                    pointerAnalysis->addBase(a, b);

                    int c = Value2Int(I->getOperand(2));
                    pointerAnalysis->addBase(a, c);
                }
                break;
            }

            case Instruction::Alloca: {
                handleAlloca(I);

                        break;
					}
				case Instruction::GetElementPtr:
					{
                    handleGetElementPtr(I);
                    break;
                }

								case Instruction::Shl:
                case Instruction::Add:
                case Instruction::Trunc: {
                    std::set<Value *> ptrs;
                    findAllPointerOperands(I, ptrs);

                    // to handle:
                    //  %4 = add i64 %3, zext (i32 ptrtoint ([24 x i32]* @tqmf
                    //  to i32) to i64)
                    //  %5 = trunc i64 %4 to i32
                    //  %s.i.0 = inttoptr i32 %5 to i32*
                    for (std::set<Value *>::iterator i = ptrs.begin(),
                                                     e = ptrs.end();
                         i != e; ++i) {
                        Value *src = *i;
                        int a = Value2Int(I);
                        int b = Value2Int(src);
                        pointerAnalysis->addBase(a, b);
                        // errs() << "I: " << *I << "\n\tfound source pointer: "
                        // << *src << "\n";
                    }
                }

                case Instruction::IntToPtr:
                case Instruction::PtrToInt: {
                    // to handle:
                    //  %s.i.0 = inttoptr i32 %5 to i32*
                    //  %.c = ptrtoint i8* %19 to i32
                    Value *src = I->getOperand(0);
                    Value *dst = I;

                    int a = Value2Int(dst);
                    int b = Value2Int(src);
                    pointerAnalysis->addBase(a, b);
                }
                case Instruction::BitCast: {
                    // %s.0 = bitcast i8* %scevgep to i32*

                    Value *src = I->getOperand(0);
                    Value *dst = I;

                        const Type *srcTy = src->getType();
						const Type *dstTy = dst->getType();

						if (srcTy->isPointerTy()) {
							if (dstTy->isPointerTy()) {
								const PointerType *PoTy = cast<PointerType>(dstTy);
								const Type *Ty = PoTy->getElementType();

								/*
								if (Ty->isStructTy()) {
									if (memoryBlock.count(src)) {
										std::vector<int> mems = memoryBlock[src];
										int parent = mems[0];

										handleNestedStructs(Ty, parent);
										memoryBlock[I] = mems;
									}
								}
								*/
							}

							int a = Value2Int(I);
							int b = Value2Int(src);
							pointerAnalysis->addBase(a, b);
							PABaseCt++;
						}

						break;
					}
				case Instruction::Store:
					{
						// *ptr = v
						StoreInst *SI = dyn_cast<StoreInst>(I);
                        Value *v = SI->getValueOperand();
                        Value *ptr = SI->getPointerOperand();

												if ( isa<Operator>(v) )
													handleOperator(v);
												if ( isa<Operator>(ptr) )
													handleOperator(ptr);

                        // handle when ptr is a GEP constant:
                        //      store i32 43, i32* getelementptr inbounds ([32
                        //      x i32]* @key, i32 0, i32 0), align 4, !tbaa !1
                        handleGetElementPtrConst(ptr);

                        if (v->getType()->isPointerTy()) {
                            int a = Value2Int(ptr);
                            int b = Value2Int(v);

                            // Handle the case when the value operand is a
                            // GetElementPtr Constant Expression
                            // For example:
                            // I: store i8* getelementptr inbounds ([2048 x
                            //      i8]* @ld_Rdbfr, i32 1, i32 0), i8**
                            //      @ld_Rdptr, align 4, !tbaa !1
                            // CE: i8* getelementptr inbounds ([2048 x i8]*
                            //      @ld_Rdbfr, i32 1, i32 0) @ld_Rdbfr =
                            //      internal global [2048 x i8]
                            //      zeroinitializer, align 1
                            handleGetElementPtrConst(v);

                            // Andrew: The code below breaks, I get an error:
                            //      "Use still stuck around after Def"
                            // Because new GEP instructions are created
                            /*
                            ConstantExpr *CE = dyn_cast<ConstantExpr>(v);
                            if (CE &&
                                CE->getOpcode() == Instruction::GetElementPtr) {

                                // Here we create an instruction from
                                // the value operand
                                Vector<Value*,4> ValueOperands;
                                for (User::op_iterator opi = ce->op_begin(), E =
                                        ce->op_end(); opi != E; ++opi)
                                    ValueOperands.push_back(cast<Value>(opi));
                                ArrayRef<Value*> Ops(ValueOperands);

                                GetElementPtrInst* gepi = 0;
                                if (cast<GEPOperator>(ce)->isInBounds())
                                    gepi = GetElementPtrInst::CreateInBounds(
                                            Ops[0], Ops.slice(1), "GEPI");
                                else
                                    gepi= GetElementPtrInst::Create(Ops[0],
                                            Ops.slice(1), "GEPI");

                                //errs() << *gepi << "\n";
                                handleGetElementPtr(gepi);
                                b = Value2Int(gepi);

                            }
                            */

                            // errs() << "  >> Pointer: " << *ptr << "\n";
                            // errs() << "  >> Value:   " << *v << "\n";

                            pointerAnalysis->addStore(a, b);
                            PAStoreCt++;
                        }

                        break;
                }
                case Instruction::Load:
					{
						// I = *ptr
                    LoadInst *LI = dyn_cast<LoadInst>(I);
                    Value *ptr = LI->getPointerOperand();

										if ( isa<Operator>(ptr) )
											handleOperator(ptr);

                    int a = Value2Int(I);
                    int b = Value2Int(ptr);
                    pointerAnalysis->addLoad(a, b);
                    PALoadCt++;

                    // int a = Value2Int(I);
                    // int b;

                    // handle a case like:
                    //   %4 = load i32* getelementptr inbounds ([18 x i32]*
                    //                  @key_P, i32 0, i32 0)
                    handleGetElementPtrConst(ptr);

                    break;
                }
                case Instruction::AtomicRMW:
					{
						// I = *ptr
						AtomicRMWInst *LI = dyn_cast<AtomicRMWInst>(I);
						Value *ptr = LI->getPointerOperand();

						int a = Value2Int(I);
						int b = Value2Int(ptr);
						pointerAnalysis->addLoad(a, b);
						PALoadCt++;

						break;
					}
				case Instruction::AtomicCmpXchg:
					{
						// I = *ptr
						AtomicCmpXchgInst *LI = dyn_cast<AtomicCmpXchgInst>(I);
						Value *ptr = LI->getPointerOperand();

						int a = Value2Int(I);
						int b = Value2Int(ptr);
						pointerAnalysis->addLoad(a, b);
						PALoadCt++;

						break;
					}
				case Instruction::PHI:
					{
						PHINode *Phi = dyn_cast<PHINode>(I);
						const Type *Ty = Phi->getType();

						if (Ty->isPointerTy()) {
							unsigned n = Phi->getNumIncomingValues();
							std::vector<Value*> values;

                            for (unsigned i = 0; i < n; i++) {
                                Value *v = Phi->getIncomingValue(i);

                                handleGetElementPtrConst(v);

                                int a = Value2Int(I);
                                int b = Value2Int(v);
                                pointerAnalysis->addBase(a, b);
                                PABaseCt++;

								values.push_back(v);

								if (phiValues.count(v)) {
									if (memoryBlocks.count(v)) {
										memoryBlocks[I] = std::vector<std::vector<int> >();
										memoryBlocks[I].insert(memoryBlocks[I].end(), memoryBlocks[v].begin(), memoryBlocks[v].end());
									}
								} else {
									if (memoryBlock.count(v)) {
										memoryBlocks[I] = std::vector<std::vector<int> >();

										if (isa<BitCastInst>(v)) {
											BitCastInst *BC = dyn_cast<BitCastInst>(v);

											Value *v2 = BC->getOperand(0);

											if (memoryBlock.count(v2)) {
												std::vector<int> mems = memoryBlock[v2];
												int parent = mems[0];
												std::vector<int> mems2 = memoryBlock2[parent];

												memoryBlocks[I].push_back(mems2);
											}
										} else
											memoryBlocks[I].push_back(memoryBlock[v]);
									}
								}
							}
						}

						break;
					}
			}

			//errs() << "\n";
		}
	}
}

// ============================= //

void PADriverTest::matchFormalWithActualParameters(Function &F) {
    if (F.arg_empty() || F.use_empty())
        return;

    /*
    */
    // errs() << "F: " << F.getName() << "\n";
    for (Value::user_iterator UI = F.user_begin(), E = F.user_end(); UI != E;
         ++UI) {
        User *U = *UI;

        // errs() << "U: " << *U << "\n";

        if (isa<ConstantExpr>(U)) {
            // %1 = call i8* bitcast (void (i8*, i8, i32)* @memset_4 to
            // i8* (i8*, i8, i64)*)(i8* bitcast ([24 x i32]* @tqmf to i8*), i8
            // 0, i64 96) #2
            U = dyn_cast<User>(U->getOperand(0));
            // assert(0);
        }

        if (isa<BlockAddress>(U))
            continue;
        if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) {
            // assert(0);
            continue;
        }

        CallSite CS(cast<Instruction>(U));
        if (CS.getCalledFunction() != &F)
            continue;

        // errs() << "U-After: " << *U << "\n";

        CallSite::arg_iterator actualArgIter = CS.arg_begin();
        Function::arg_iterator formalArgIter = F.arg_begin();
        int size = F.arg_size();

        for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
            Value *actualArg = *actualArgIter;

            // handle a case like:
            //  tail call fastcc void @upzero(i32 %127, i32* getelementptr
            //  inbounds ([6 x i32]* @delay_dltx, i32 0, i32 0), i32*
            //  getelementptr inbounds ([6 x i32]* @delay_bpl, i32 0, i32 0))
            handleGetElementPtrConst(actualArg);
            // ConstantExpr *CE = dyn_cast<ConstantExpr>(actualArg);
            // if (CE && CE->getOpcode() == Instruction::GetElementPtr) {
            // actualArg = CE->getOperand(0);
            //}

            Value *formalArg = formalArgIter;

            int a = Value2Int(formalArg);
            int b = Value2Int(actualArg);
			pointerAnalysis->addBase(a, b);
            PABaseCt++;
        }
    }
}

// ============================= //

void PADriverTest::matchReturnValueWithReturnVariable(Function &F) {
//	if (F.getReturnType()->isVoidTy() || F.mayBeOverridden()) return;
	if (F.getReturnType()->isVoidTy() ) return;

	std::set<Value*> retVals;

	for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
		if (ReturnInst *RI = dyn_cast<ReturnInst>(BB->getTerminator())) {
			Value *v = RI->getOperand(0);

			retVals.insert(v);
        }
    }

    for (Value::user_iterator UI = F.user_begin(), E = F.user_end(); UI != E;
         ++UI) {
        // User *U = *UI;
        CallSite CS(*UI);
        Instruction *Call = CS.getInstruction();

        if (!Call || CS.getCalledFunction() != &F)
            continue;

        if (Call->use_empty())
            continue;

        for (std::set<Value*>::iterator it = retVals.begin(), E = retVals.end(); it != E; ++it) {

			int a = Value2Int(CS.getCalledFunction());
			int b = Value2Int(*it);
			pointerAnalysis->addBase(a, b);
			PABaseCt++;
		}
	}
}

// ============================= //

void PADriverTest::handleAlloca(Instruction *I) {
	AllocaInst *AI = dyn_cast<AllocaInst>(I);
	const Type *Ty = AI->getAllocatedType();

	std::vector<int> mems;
	unsigned numElems = 1;
	bool isStruct = false;

//TODO: Struct & Memory Bank Wise
/*
	if (Ty->isStructTy()) { // Handle structs
		const StructType *StTy = dyn_cast<StructType>(Ty);
		numElems = StTy->getNumElements();
		isStruct = true;
	}
*/
	if (!memoryBlock.count(I)) {
		for (unsigned i = 0; i < numElems; i++) {
			mems.push_back(getNewMemoryBlock());

/*
			if (isStruct) {
				const StructType *StTy = dyn_cast<StructType>(Ty);

				if (StTy->getElementType(i)->isStructTy()) {
					handleNestedStructs(StTy->getElementType(i), mems[i]);
				}
			}
*/
		}

		memoryBlock[I] = mems;
	} else {
		mems = memoryBlock[I];
	}

	for (unsigned i = 0; i < mems.size(); i++) {
		int a = Value2Int(I);
		pointerAnalysis->addAddr(a, mems[i]);
		PAAddrCt++;
	}
}

// ============================= //

void PADriverTest::handleGlobalVariable(GlobalVariable *G) {
	const Type *Ty = G->getType();

	std::vector<int> mems;
	unsigned numElems = 1;
	bool isStruct = false;

/*
	if (Ty->isStructTy()) { // Handle structs
		const StructType *StTy = dyn_cast<StructType>(Ty);
		numElems = StTy->getNumElements();
		isStruct = true;
	}
*/

	if (!memoryBlock.count(G)) {
		for (unsigned i = 0; i < numElems; i++) {
			mems.push_back(getNewMemoryBlock());
/*
			if (isStruct) {
				const StructType *StTy = dyn_cast<StructType>(Ty);

				if (StTy->getElementType(i)->isStructTy())
					handleNestedStructs(StTy->getElementType(i), mems[i]);
			}
*/
		}

		memoryBlock[G] = mems;
	} else {
		mems = memoryBlock[G];
	}

	for (unsigned i = 0; i < mems.size(); i++) {
		int a = Value2Int(G);
		pointerAnalysis->addAddr(a, mems[i]);
		PAAddrCt++;
    }
}

// handle cases where there is a constant getelementptr value as an operand:
//      store i32 43, i32* getelementptr inbounds ([32 x i32]* @key, i32 0, i32
//      0), align 4, !tbaa !1
void PADriverTest::handleGetElementPtrConst(Value *op) {
    ConstantExpr *CE = dyn_cast<ConstantExpr>(op);
    if (CE && CE->getOpcode() == Instruction::GetElementPtr) {
        // errs() << "op: " << *op << "\n";
        Value *mem = CE->getOperand(0);
        // errs() << "mem: " << *mem << "\n";

        int src = Value2Int(mem);
        int dst = Value2Int(op);
        pointerAnalysis->addBase(dst, src);
        PABaseCt++;
    }
}

// ============================= //

void PADriverTest::handleGetElementPtr(Instruction *I) {

	//errs() << "INSIDE GetElementPtrInst \n";

	GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I);
	Value *v = GEPI->getPointerOperand();
	if ( isa<Operator>(v) )
		handleOperator(v);

	const PointerType *PoTy = cast<PointerType>(GEPI->getPointerOperandType());
	const Type *Ty = PoTy->getElementType();

	/*
	if (Ty->isStructTy()) {
		if (phiValues.count(v)) {
			std::vector<Value*> values = phiValues[v];

			for (unsigned i = 0; i < values.size(); i++) {
				Value* vv = values[i];

				if (memoryBlocks.count(vv)) { //memoryBlocks O 
					for (unsigned j = 0; j < memoryBlocks[vv].size(); j++) {
						int i = 0;
						unsigned pos = 0;
						bool hasConstantOp = true;

						for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
							if (i == 1) {
								if (isa<ConstantInt>(*it))
									pos = cast<ConstantInt>(*it)->getZExtValue();
								else
									hasConstantOp = false;
							}

							i++;
						}
						if (hasConstantOp) {
							std::vector<int> mems = memoryBlocks[vv][j];
							int a = Value2Int(I);
							if (pos < mems.size()) {
								pointerAnalysis->addAddr(a, mems[pos]);
								PAAddrCt++;
							}
						}
					}
				} else { //memoryBlocks X
					if (memoryBlock.count(vv)) { //memoryBlock O
						if (isa<BitCastInst>(vv)) { //bitcast O
							BitCastInst *BC = dyn_cast<BitCastInst>(vv);

							Value *v2 = BC->getOperand(0);

							if (memoryBlock.count(v2)) {
								int i = 0;
								unsigned pos = 0;
								bool hasConstantOp = true;

								for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
									if (i == 1) {
										if (isa<ConstantInt>(*it))
											pos = cast<ConstantInt>(*it)->getZExtValue();
										else
											hasConstantOp = false;
									}

									i++;
								}

								if (hasConstantOp) {
									std::vector<int> mems = memoryBlock[v2];
									int parent = mems[0];
									if (memoryBlock2.count(parent)) {
										std::vector<int> mems2 = memoryBlock2[parent];

										int a = Value2Int(I);
										if (pos < mems2.size()) {
											pointerAnalysis->addAddr(a, mems2[pos]);
											PAAddrCt++;
										}
									}
								}
							}
						} else { //bitcast X
							int i = 0;
							unsigned pos = 0;
							bool hasConstantOp = true;

							for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
								if (i == 1) {
									if (isa<ConstantInt>(*it))
										pos = cast<ConstantInt>(*it)->getZExtValue();
									else
										hasConstantOp = false;
								}

								i++;
							}

							if (hasConstantOp) {
								std::vector<int> mems = memoryBlock[vv];
								int a = Value2Int(I);
								//pointerAnalysis->addBase(a, mems[pos]);
								if (pos < mems.size()) {
									pointerAnalysis->addAddr(a, mems[pos]);
									PAAddrCt++;
								}
							}
						}
					} else { //memoryBlock X
						GetElementPtrInst *GEPI2 = dyn_cast<GetElementPtrInst>(vv);

						if (!GEPI2) return;

						Value *v2 = GEPI2->getPointerOperand();

						if (memoryBlock.count(v2)) {
							int i = 0;
							unsigned pos = 0;
							bool hasConstantOp = true;

							for (User::op_iterator it = GEPI2->idx_begin(), e = GEPI2->idx_end(); it != e; ++it) {
								if (i == 1) {
									if (isa<ConstantInt>(*it))
										pos = cast<ConstantInt>(*it)->getZExtValue();
									else
										hasConstantOp = false;
								}

								i++;
							}

							if (hasConstantOp) {
								std::vector<int> mems = memoryBlock[v2];
								if (pos < mems.size()) {
									int parent = mems[pos];

									i = 0;
									unsigned pos2 = 0;

									for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
										if (i == 1)
											pos2 = cast<ConstantInt>(*it)->getZExtValue();

										i++;
									}

									if (memoryBlock2.count(parent)) {
										std::vector<int> mems2 = memoryBlock2[parent];
										int a = Value2Int(I);

										if (pos2 < mems2.size()) {
											pointerAnalysis->addAddr(a, mems2[pos2]);
											PAAddrCt++;
											memoryBlock[v] = mems2;
										}
									}

								}
							}
						}
					} //memoryBlock end
				} //memoryBlocks end
			} //for values end
		} else { // phi Count end and phi X start
			if (memoryBlock.count(v)) { //memoryBlock O
				//v ( Pointer Value ) directly points memory object ( e.g. alloca )

				if (isa<BitCastInst>(v)) { // bitcast O
					BitCastInst *BC = dyn_cast<BitCastInst>(v);

					Value *v2 = BC->getOperand(0);

					if (memoryBlock.count(v2)) {
						int i = 0;
						unsigned pos = 0;
						bool hasConstantOp = true;

						for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
							if (i == 1) {
								if (isa<ConstantInt>(*it))
									pos = cast<ConstantInt>(*it)->getZExtValue();
								else
									hasConstantOp = false;
							}

							i++;
						}

						if (hasConstantOp) {
							std::vector<int> mems = memoryBlock[v2];
							int parent = mems[0];
							if (memoryBlock2.count(parent)) {
								std::vector<int> mems2 = memoryBlock2[parent];

								int a = Value2Int(I);
								if (pos < mems2.size()) {
									pointerAnalysis->addAddr(a, mems2[pos]);
									PAAddrCt++;
								}
							}
						}
					}
				} else { //bitcast X
					int i = 0;
					unsigned pos = 0;
					bool hasConstantOp = true;

					for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
						if (i == 1) {
							if (isa<ConstantInt>(*it))
								pos = cast<ConstantInt>(*it)->getZExtValue();
							else
								hasConstantOp = false;
						}

						i++;
					}

					if (hasConstantOp) {
						std::vector<int> mems = memoryBlock[v];
						int a = Value2Int(I);
						//pointerAnalysis->addBase(a, mems[pos]);
						if (pos < mems.size()) {
							pointerAnalysis->addAddr(a, mems[pos]);
							PAAddrCt++;
						}
					}
				}
			} else { //memoryBlock X
				GetElementPtrInst *GEPI2 = dyn_cast<GetElementPtrInst>(v);

				if (!GEPI2) return;

				Value *v2 = GEPI2->getPointerOperand();

				if (memoryBlock.count(v2)) {
					int i = 0;
					unsigned pos = 0;
					bool hasConstantOp = true;

					for (User::op_iterator it = GEPI2->idx_begin(), e = GEPI2->idx_end(); it != e; ++it) {
						if (i == 1) {
							if (isa<ConstantInt>(*it))
								pos = cast<ConstantInt>(*it)->getZExtValue();
							else
								hasConstantOp = false;
						}

						i++;
					}

					if (hasConstantOp) {
						std::vector<int> mems = memoryBlock[v2];
						if (pos < mems.size()) {
							int parent = mems[pos];

							i = 0;
							unsigned pos2 = 0;

							for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
								if (i == 1)
									pos2 = cast<ConstantInt>(*it)->getZExtValue();

								i++;
							}

							if (memoryBlock2.count(parent)) {
								std::vector<int> mems2 = memoryBlock2[parent];
								int a = Value2Int(I);

								if (pos2 < mems2.size()) {
									pointerAnalysis->addAddr(a, mems2[pos2]);
									PAAddrCt++;
									memoryBlock[v] = mems2;
								}
							}
						}
					}
				}
			} //memoryBlock end
		} //phi end
	} else { // isStruct X
	*/
		//errs() << "  >> I: " << *I << "\n";
		//errs() << "  >> v: " << *v << "\n";
		int a = Value2Int(I);
		int b = Value2Int(v);
		pointerAnalysis->addBase(a, b);
		PABaseCt++;
//	}
}

// ============================= //2

void PADriverTest::handleNestedStructs(const Type *Ty, int parent) {
	const StructType *StTy = dyn_cast<StructType>(Ty);
	unsigned numElems = StTy->getNumElements();
	std::vector<int> mems;

	for (unsigned i = 0; i < numElems; i++) {
		mems.push_back(getNewMemoryBlock());

		if (StTy->getElementType(i)->isStructTy())
			handleNestedStructs(StTy->getElementType(i), mems[i]);
	}

	memoryBlock2[parent] = mems;

	for (unsigned i = 0; i < mems.size(); i++) {
		pointerAnalysis->addAddr(parent, mems[i]);
		PAAddrCt++;
	}
}

// ============================= //

int PADriverTest::getNewMemoryBlock() {
	return nextMemoryBlock++;
}

// ============================= //

int PADriverTest::getNewInt() {
	return getNewMemoryBlock();
}

// ============================= //

int PADriverTest::Value2Int(Value *v) {

	int n;
	SmallString<256> nameBuf;
	StringRef name;

	if (value2int.count(v))
		return value2int[v];

	n = getNewInt();
	value2int[v] = n;
	int2value[n] = v;

	// Also get a name for it
	if (v->hasName()) {
		nameMap[n] = v->getName();
	}
	else {
		if ( Instruction *inst = dyn_cast<Instruction>(v) ) {
			nameBuf.clear();
			if (isa<Constant>(v))
				name = Twine(Twine("constant_")+Twine(inst->getOpcodeName())).toStringRef(nameBuf);
			else
				name = Twine(Twine("not_constant_")+Twine(inst->getOpcodeName())).toStringRef(nameBuf);
			// run with -instnamer to avoid this:
			nameMap[n] = name.str();
		}
		else {
			if (isa<Constant>(v))
				nameMap[n] = "constant";
			else
				nameMap[n] = "not_constant";
		}
	}

	return n;
}

// ========================================= //

// Register the pass to the LLVM framework
 char PADriverTest::ID = 0;
 static RegisterPass<PADriverTest> X("pa-test", "Pointer Analysis Driver Pass", false, false);
