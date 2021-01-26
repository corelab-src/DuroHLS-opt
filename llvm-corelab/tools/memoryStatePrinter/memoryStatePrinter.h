#ifndef CORELAB_MSP_H
#define CORELAB_MSP_H
#include <inttypes.h>


extern "C" void printBits (int type, char *name, void *src, int size, int memBitWidth, 
													bool isStruct, int id);

extern "C" void printEnd (char *name);

extern "C" void registerStruct (int id);

extern "C" void startStruct (void);

extern "C" void addElement (int id, int dataWidth, int num);

#endif
