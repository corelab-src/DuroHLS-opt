#include <stdio.h>
#include <unordered_map>
#include <list>
#include <iostream>
#include <fstream>
#include <assert.h>

#include "execRuntime.h"
#include "x86timer.hpp"

std::unordered_map<int, double> *execTimeOfFunction;
std::unordered_map<int, double> *tmpTimeOfFunction;
std::list<int> *functionStack;

x86timer *t;
double total_time;

extern "C"
void plainExecInitialize(int mainID, int nContext) {
	printf("plain exec start\n");

	execTimeOfFunction = new std::unordered_map<int, double>();
	tmpTimeOfFunction = new std::unordered_map<int, double>();
	functionStack = new std::list<int>();
	t = new x86timer();

	for (int i=1; i <= nContext; i++) 
	{
		(*execTimeOfFunction)[i] = 0.0;
		(*tmpTimeOfFunction)[i] = 0.0;
	}
	
	(*tmpTimeOfFunction)[mainID] = t->now();
	total_time = (*tmpTimeOfFunction)[mainID];
	(*functionStack).push_back(mainID);

}

extern "C"
void plainExecFinalize(int mainID, int nContext, int accum) {
	(*execTimeOfFunction)[mainID] = t->now() - (*tmpTimeOfFunction)[mainID];

	printf("plain exec finalizing\n");

	if ( accum == 0 ) {
		(*functionStack).pop_back();
		assert((*functionStack).size() == 0);
	}

	//printing
	std::ofstream execfile("ExecTime.data", std::ios::out | std::ofstream::binary);
	
	if ( accum == 0 )
		execfile << "Pure Function Execution Time\n\n";
	else
		execfile << "Accumulation Function Execution Time\n\n";

	for (int i=1; i <= nContext; i++)
		execfile << "FunctionID " << i << "\t:\t" << (*execTimeOfFunction)[i] << "\n";

	execfile.close();
}


extern "C"
void plainExecCallSiteBegin (int funcID, int accum) {
	
	if ( accum == 0 )
		(*functionStack).push_back(funcID);

	(*tmpTimeOfFunction)[funcID] = t->now();

}

extern "C"
void plainExecCallSiteEnd  (int funcID, int accum) {

	double executionTime = t->now() - (*tmpTimeOfFunction)[funcID];
	(*execTimeOfFunction)[funcID] += executionTime;

	if ( accum == 0 ) {
		int pop = (*functionStack).back();
		assert(pop == funcID);
		(*functionStack).pop_back();

		for ( int stacked : (*functionStack) )
			(*tmpTimeOfFunction)[stacked] += executionTime;
	}

}

