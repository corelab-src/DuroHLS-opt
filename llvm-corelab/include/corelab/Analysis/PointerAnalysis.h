#ifndef POINTER_ANALYSIS_H
#define POINTER_ANALYSIS_H

#include <set>
#include <map>
#include <deque>
#include <ostream>
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DenseMap.h"

using namespace llvm;
using namespace std;

namespace corelab
{

// ============================================= //

typedef std::set<int> IntSet;
typedef DenseMap<int, IntSet> IntSetMap;
typedef DenseMap<int, int> IntMap;
typedef std::deque<int> IntDeque;

// ============================================= //

class PointerAnalysisTest {

    public:
        PointerAnalysisTest();
        ~PointerAnalysisTest();

        // Add a constraint of type: A = &B
        void addAddr(int A, int B);

        // Add a constraint of type: A = B
        void addBase(int A, int B);

        // Add a constraint of type: *A = B
        void addStore(int A, int B);

        // Add a constraint of type: A = *B
        void addLoad(int A, int B);

        // Execute the pointer analysis
        void solve(bool withCycleRemoval = true);

        // Return the set of positions pointed by A:
        //   pointsTo(A) = {B1, B2, ...}
        std::set<int> &pointsTo(int A);

        // Return the points-to map
        DenseMap<int, std::set<int>> &allPointsTo();

        // Print the current state (graph, representatives and points-to)
        void print(raw_ostream &O);

        // Print the graph (DOT format)
        void printDot(std::ostream &output, std::string graphName,
                      DenseMap<int, std::string> names);

        // Get the amount of merged vertices
		int getNumOfMertgedVertices();
		int getNumCallsRemove();
		int getNumVertices();

	private:
		void addNode(int id);
		void addEdge(int fromId, int toId);
		void addToPts(int pointed, int pointee);
		bool comparePts(int a, int b);
		void cycleSearch(int source, int target);
		void merge(int id, int target);

		// Hold the points-to Set
		IntSetMap pointsToSet;

		// Hold the vertices and their representatives
        IntMap vertices;
		int numMerged;
		int numCallsRemove;

		// Hold the active vertices
		IntSet activeVertices;

		// Hold the graph structure
        IntSetMap from;
        IntSetMap to;

		// Hold the complex constraints
        IntSetMap loads;
        IntSetMap stores;
};

// ============================================= //

}

#endif  /* POINTER_ANALYSIS_H */
