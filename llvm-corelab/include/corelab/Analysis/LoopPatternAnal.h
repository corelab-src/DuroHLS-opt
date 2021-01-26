//Written by csKim
#ifndef LLVM_CORELAB_HLSLOOPANAL_H
#define LLVM_CORELAB_HLSLOOPANAL_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/DenseMap.h"

#include "corelab/Analysis/PADriver.h"

#include <set>
#include <list>
#include <vector>
#include <iostream>
#include <fstream>

namespace corelab
{
	using namespace llvm;
	using namespace std;

	struct ElementInfo{
		unsigned dataWidth;
		unsigned numOfElements;
	};



	class MemObj
	{
		public:
			MemObj(Value *, bool, bool, const DataLayout &);

			StringRef getName(void)	{	return v->getName(); };
			Value *getValue(void) { return v; };

			bool isCallInstBased(void) { return isCallInst; };
			bool isExternalValue(void) { return isExternal; };

		// ---------------Get Memory Information ----------//
			unsigned getNumOfElements(void) { return numOfElements; };
			unsigned getDataBitSize(void) { return dataBitSize; };
			unsigned getDimSize(unsigned);
			unsigned getMaxDimSize(void) { return maxDim; };
		
		private:
			ElementInfo getElementInfo(Type *, const DataLayout &);

			Value *v;
			Type *ty;
			Type *eTy;

			bool isCallInst;
			bool isExternal;

			list<unsigned> nestArrayStructure;
			//DIM : Array[4][3][2][1]
			DenseMap<unsigned, unsigned> dim2Size;
			unsigned maxDim;

			unsigned numOfElements;
			unsigned dataBitSize;
	};

	//Access Pattern of an Object in a Loop
	class AccessPattern
	{
		public:
			AccessPattern(const Loop *L, MemObj *memObj_) : outMostLoop(L), memObj(memObj_) {}

			enum SimpleAP{
				 CONST, ROW, COLUMN, MIXED, RANDOM, UNKNOWN
			};
			enum MemType{
				LOAD, STORE, BOTH
			};

			SimpleAP getSimpleAP(void) { return simpleAP; };
			MemObj *getMemObj(void) { return memObj; };

			list<Instruction *> getInstList(void) { return accessInstList; };
			unsigned getNestOfInst(Instruction *inst) { return inst2Nest[inst]; };
			vector<pair<unsigned, unsigned>> getStridePair(Instruction *inst)
			{	return inst2StridePair[inst]; };

			SimpleAP getSAPOfInst(Instruction *inst) 
			{ 
				if (inst2SAP.count(inst))
					return inst2SAP[inst]; 
				else
					return UNKNOWN;
			}

			StringRef getAPName(SimpleAP sap)
			{
				switch(sap)
				{
					case ROW:
						return StringRef("ROW");
					case COLUMN:
						return StringRef("COLUMN");
					case MIXED:
						return StringRef("MIXED");
					case RANDOM:
						return StringRef("RANDOM");
					case CONST:
						return StringRef("CONST");
					case UNKNOWN:
						return StringRef("UNKNOWN");
				}
			}

			// -------------Build Up Methods----------------//
			void setSimpleAP(SimpleAP sap) { simpleAP = sap; };

			void setInst2Nest(Instruction *inst, unsigned nest) { inst2Nest[inst] = nest; };
			void insertAccessInstList(Instruction *inst) { accessInstList.push_back(inst); };
			void setInst2StridePair(Instruction *inst, unsigned nest, unsigned stride)
			{	inst2StridePair[inst].push_back(make_pair(nest,stride)); };
			void setInst2SAP(Instruction *inst, SimpleAP sap) { inst2SAP[inst] = sap; };
		
		private:
			const Loop *outMostLoop;
			MemObj *memObj;

			SimpleAP simpleAP;

			list<Instruction *> accessInstList;
			DenseMap<Instruction *, unsigned> inst2Nest;
			DenseMap<Instruction *, vector<pair<unsigned, unsigned>>> inst2StridePair;
			DenseMap<Instruction *, SimpleAP> inst2SAP;

//			DenseMap<unsigned, unsigned> nest2Stride;
//			DenseMap<unsigned, MemType> nest2Type;
	};

	class LoopNode
	{
		public:
			LoopNode(const Loop *L) : outMostLoop(L) {}

			StringRef getName(void) { return outMostLoop->getName(); };
			const Loop *getOutMostLoop(void) { return outMostLoop; };
			bool getObjDetermined(void) { return determined; };
			bool getStructureDetermined(void) { return Sdetermined; };

			// --------------Get Memory Information----------------//

			AccessPattern *getAccessPattern(MemObj *memObj) { return memObj2AP[memObj]; };
			set<MemObj *> getUsedMemObjFromNest(unsigned nest) { return nest2MemObj[nest]; };

			//structure fail can use
			set<MemObj *> getUsedMemObjList(void) { return usedMemObjList; };
			MemObj *getMemObjFromInst(Instruction *inst) { return memInst2Obj[inst]; }; 
			set<Instruction *> getInstSet(MemObj *obj) { return obj2Inst[obj]; }; 
			
			const Loop *getLoopOfInst(Instruction *inst) { return inst2Loop[inst]; };

			// --------------Get Loop Information-------------------//

			unsigned getMaxNestLevel(void) { return maxNestLevel; };
			const Loop *getLoopFromNest(unsigned nestLevel) { return nest2Loop[nestLevel]; };
			unsigned getNestFromLoop(const Loop *loop) 
			{ 
				for ( auto mapIter : nest2Loop )
				{
					if (mapIter.second == loop)
						return mapIter.first;
				}
				return 0;
			}
			unsigned getNestFromIndV(PHINode *indV) { return indV2Nest[indV]; };
			unsigned getIterCount(unsigned nestLevel) { return nest2Iter[nestLevel]; };

			list<Instruction *> getFailList(void) { return failedList; };

			// --------------Build Up Methods---------------//

			void setObjDetermined(bool dm) { determined = dm; };
			void setStructureDetermined(bool sdm) { Sdetermined = sdm; };

			void insertMemObj(MemObj *memObj) { usedMemObjList.insert(memObj); };
			void insertMemObjAP(MemObj *memObj, AccessPattern *AP) { memObj2AP[memObj] = AP; };
			void setNestMemObj(unsigned nest, MemObj *memObj) { nest2MemObj[nest].insert(memObj); };

			void setMaxNestLevel(unsigned mnl) { maxNestLevel = mnl; };
			void setIndV2Nest(PHINode *indV, unsigned nest) { indV2Nest[indV] = nest; };
			void setLoopFromNest(unsigned nestLevel, const Loop *tL) { nest2Loop[nestLevel] = tL; };
			void setIterCount(unsigned nestLevel, unsigned iterCount) 
			{ nest2Iter[nestLevel] = iterCount; };

			void setMemInst2MemObj(Instruction *inst, MemObj *memObj) { memInst2Obj[inst] = memObj; };

			void setObj2Inst(MemObj *obj, Instruction *inst) { obj2Inst[obj].insert(inst); };
			void setInst2Loop(Instruction *inst, const Loop *lp) { inst2Loop[inst] = lp; };

			void setFailInst(Instruction *inst) { failedList.push_back(inst); };

		private:

			const Loop *outMostLoop;
			bool determined;
			bool Sdetermined;
		
			set<MemObj *> usedMemObjList;
			DenseMap<MemObj *, AccessPattern *> memObj2AP;
			DenseMap<unsigned, set<MemObj *>> nest2MemObj;
			DenseMap<PHINode *, unsigned> indV2Nest;

			DenseMap<Instruction *, MemObj *> memInst2Obj;
			DenseMap<MemObj *, set<Instruction *>> obj2Inst;
			DenseMap<Instruction *, const Loop *> inst2Loop;
			
			unsigned maxNestLevel;
			DenseMap<unsigned, const Loop *> nest2Loop;
//			DenseMap<const Loop *,unsigned> loop2Nest;
			DenseMap<unsigned, unsigned> nest2Iter;

			list<Instruction *> failedList;
	};

	class LoopAliasInfoTest
	{
		public:
			LoopAliasInfoTest(const Loop *L, list<BasicBlock *> bb, list<Instruction *> exitList_) : 
													targetL(L), targetBB(bb), exitList(exitList_), variousDist(false) {}

			struct AccessInfo{
				Value *value;
				bool store;
				bool array;
				bool constant;
				list<Instruction *> operationList;
				//TODO : the location of constant access
			} AccessInfo_;

			enum DepType{
				Intra, Inter, Constant
			};
			struct AliasInfo{
				DepType type;
				int distance;
			} AliasInfo_;

			// --------- Usage -----------
			const Loop *getLoop(void) { return targetL; };
			list<BasicBlock *> getBB(void) { return targetBB; };
			PHINode *getIndPHINode(void) { return indNode; };
			list<Instruction *> getInterDefList(Instruction *inst) { return interDefList[inst]; };
			list<Instruction *> getInterUseList(Instruction *inst) { return interUseList[inst]; };

			bool getObjDetermined(void) { return objDetermined; };
			AccessInfo getAccessInfo(Instruction *inst) { return inst2AInfo[inst]; };

			list<pair<Instruction *, AliasInfo>> getUseAliasList(Instruction *inst) {
				return memUseMap[inst];
			}
			list<pair<Instruction *, AliasInfo>> getDefAliasList(Instruction *inst) {
				return memDefMap[inst];
			}

			bool isVariousDist(void) { return variousDist; };

			// --------- Build -----------
			void setIndPHINode(PHINode *ind) { indNode = ind; };
			void setInterDefList(Instruction *use, Instruction *def) 
			{ interDefList[use].push_back(def); };
			void setInterUseList(Instruction *use, Instruction *def) 
			{ interUseList[def].push_back(use); };

			void setObjDetermined(bool dd) { objDetermined = dd; };
			void setInst2AInfo(Instruction *inst, AccessInfo ai) { inst2AInfo[inst] = ai; };
			AccessInfo genAInfo(Value *v, bool st, bool arr, 
					bool constant, list<Instruction *> &operList) { 
				AccessInfo ai;
				ai.value = v;
				ai.store = st;
				ai.array = arr;
				ai.constant = constant;
				ai.operationList = operList;
				return ai;
			}
			void setUseAliasInfo(Instruction *use, Instruction *key, 
					DepType dt, int dist) {
				AliasInfo ai;
				ai.type = dt;
				ai.distance = dist;
				memUseMap[key].push_back(make_pair(use, ai));
			}
			void setDefAliasInfo(Instruction *def, Instruction *key,
					DepType dt, int dist) {
				AliasInfo ai;
				ai.type = dt;
				ai.distance = dist;
				memDefMap[key].push_back(make_pair(def, ai));
			}

			void setVariousDist(void) { bool variousDist = true; };

		private:
			const Loop *targetL;
			list<BasicBlock *> targetBB;
			list<Instruction *> exitList;
			PHINode *indNode;

			bool objDetermined;
			bool variousDist;

			DenseMap<Instruction *, list<Instruction *>> interUseList;
			DenseMap<Instruction *, list<Instruction *>> interDefList;
			DenseMap<Instruction *, AccessInfo> inst2AInfo;
			// Write Instruction -> set of instruction that alias the address after write
			DenseMap<Instruction *, list<pair<Instruction *, AliasInfo>>> memUseMap;
			// Read Instruction -> set of write instruction that alias the address after read
			DenseMap<Instruction *, list<pair<Instruction *, AliasInfo>>> memDefMap;
	};

	class LPA : public ModulePass
	{
		public:
			static char ID;
			LPA() : ModulePass(ID) {};

			virtual void getAnalysisUsage( AnalysisUsage &AU ) const;

			StringRef getPassName() const { return "Loop Pattern Analysis"; };

			bool runOnModule(Module& M);
			void analysisOnLoop(const Loop *L);
			void collectAliasInfo(const Loop *, list<BasicBlock *> &, list<Instruction *> &);
		
			// --------- Usage -----------
			list<MemObj *> getMemObjList(void) { return memObjList; };
			MemObj *getMemObjFromValue(Value *v) { return v2MemObj[v]; };

			list<LoopNode *> getLoopNodeList(void) { return loopNodeList; };
			LoopNode *getLoopNodeFromLoop(const Loop *l) 
			{ 
				for ( auto LNIter : loopNodeList )
				{
					if (LNIter->getOutMostLoop() == l)
						return LNIter;
				}
				return NULL;
			}
			LoopAliasInfoTest *getLoopAFromLoop(const Loop *l)
			{
				for ( auto LAIter : loopAList )
				{
					if (LAIter->getLoop() == l)
						return LAIter;
				}
				return NULL;
			}
			bool isBBForPipeline(BasicBlock *bb) {
				if ( bb2Pipeline.count(bb) )
					return bb2Pipeline[bb];
				else
					return false;
			}
			LoopAliasInfoTest *getLoopAFromBB(BasicBlock *bb) {
				for ( auto LAIter : loopAList )
					for ( auto bi : LAIter->getBB() )
						if (bi == bb)
							return LAIter;
				return NULL;
			}
			set<LoopNode> getLNListOfMemObj(MemObj *mem) { return memObj2LoopNode[mem]; };

			// --------- Memory Object -----------
			void searchGV();
			void searchAlloca();
			void searchAllocaCall();

			// --------- Build LoopNode -----------
			void setLNList(LoopNode *LN) { loopNodeList.push_back(LN); };
			void setLAList(LoopAliasInfoTest *LA) { loopAList.push_back(LA); };
//			void setL2LN(const Loop *l, LoopNode *LN) { loop2LoopNode[l] = LN; };

			PHINode *getCanonicalInductionVariableAux(const Loop*);
			PHINode *getCanonicalInductionVariableAuxForAlias(const Loop*);
			bool setLoopStructure(LoopNode *);

			void collectUsedObjects(const Loop *, unsigned, LoopNode *);
			MemObj *traceUsedObject(Value *, unsigned);
			list<Instruction *> getCallInstList(Function *);
			bool simpleLoopCheck(const Loop *, unsigned *);

			void collectLoopPattern(LoopNode *);
			bool collectIndStride(DenseMap<PHINode *, unsigned> &, 
					GetElementPtrInst *, MemObj *);
			std::pair<PHINode*, unsigned> getStride(Value *);
			unsigned getStrideSizeFromArray(MemObj *, unsigned);

			bool isUniqueBB(const Loop *, const BasicBlock *);
			unsigned getIterationCount(const Loop *);
			BasicBlock *getOutsideExitBlock(const Loop *);

			bool collectIndOperation(LoopAliasInfoTest *, Instruction *, 
					list<Instruction *>&, GetElementPtrInst *, PHINode *);
			int getRefIndex(list<Instruction *> &, int);

			//Alias for Pipelining
			const Loop *getInnerMostLoop(const Loop *);
			list<BasicBlock *> getOnlyOneBB(const Loop *);
			void setBBForPipeline(list<BasicBlock *> bb) { 
				for ( auto bi : bb )
					bb2Pipeline[bi] = true;
			}
			list<Instruction *> getExitList(list<BasicBlock *> &);
			bool collectCMPOperation(list<Instruction *>&, list<BasicBlock *> &, Instruction *);

			// --------- print -----------
			void printLoopPattern(const Loop *, raw_fd_ostream &);
			void printAllInfo(raw_fd_ostream &);
			void printMemoryObject(raw_fd_ostream &);
			void printAliasInfo(raw_fd_ostream &);
			void printRegDependence(raw_fd_ostream &, LoopAliasInfoTest *);
			void printMemDependence(raw_fd_ostream &, LoopAliasInfoTest *);

		private:
			Module *module;
//			LoopAA *loopaa;
			LoopInfo *li;
			PADriverTest *pa;

			DenseMap<Function *, Instruction *> fcn2CallInst;

			// ---------- Memory Object -----------
			list<MemObj *> memObjList;
			DenseMap<Value *, MemObj *> v2MemObj;

			// ---------- Loop Node --------------
			list<LoopNode *> loopNodeList;
			list<LoopAliasInfoTest *> loopAList;
//			DenseMap<const Loop *, LoopNode *> loop2LoopNode;
			DenseMap<MemObj *, set< LoopNode >> memObj2LoopNode;
			DenseMap<const Function *, LoopInfo *> loopInfoOf;

			DenseMap<BasicBlock *, bool> bb2Pipeline;
	};

}

#endif
