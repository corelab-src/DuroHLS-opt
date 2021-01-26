#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "memoryStatePrinter.h"

#include <map>

#define MASK16 0x00FF
#define MASK32 0x000000FF
#define MASK64 0x00000000000000FF

std::map<int, int *> id2struct;

static int offset;

extern "C" 
void printBits (int dataWidth, char *name, void *src, int numOfElements, int memBitWidth,
								bool isStruct, int id)
{
	int i, j, k, n;

	fprintf(stderr, "%s\n", name);

	if ( isStruct && memBitWidth == 0 ) { // struct without memoryblock
		int *obj = id2struct[id];
		if ( obj[0] != 0 ) { // not array of struct
			// the dataWidth is the maximum dataWidth
			for ( i=0; obj[i] != 0; )
			{
				int currentWidth = obj[i];
				i++;
				int currentNum = obj[i];
				i++;

				for ( j=0; j < currentNum; j++ )
				{
					if ( currentWidth == 2 ) { // pointer width is considered as 32bits in hls
						int **src_p = (int **)src;

						if ( dataWidth == 2 )
							fprintf(stderr, "PPPPPPPP\n");
						else if ( dataWidth == 32 )
							fprintf(stderr, "PPPPPPPP\n");
						else if ( dataWidth == 64 )
							fprintf(stderr, "PPPPPPPPPPPPPPPP\n");

						src = ++src_p;
					}
					else if ( currentWidth == 8 ) {
						uint8_t *src_8 = (uint8_t *)src;

						if ( dataWidth == 8 )
							fprintf(stderr, "%02x\n", src_8[0]);
						else if ( dataWidth == 16 )
							fprintf(stderr, "%04x\n", src_8[0]);
						else if ( dataWidth == 32 )
							fprintf(stderr, "%08x\n", src_8[0]);
						else if ( dataWidth == 64 )
							fprintf(stderr, "%016x\n", src_8[0]);

						src = ++src_8;
					}
					else if ( currentWidth == 16 ) {
						uint16_t *src_16 = (uint16_t *)src;

						if ( dataWidth == 16 )
							fprintf(stderr, "%04x\n", src_16[0]);
						else if ( dataWidth == 32 )
							fprintf(stderr, "%08x\n", src_16[0]);
						else if ( dataWidth == 64 )
							fprintf(stderr, "%016x\n", src_16[0]);

						src = ++src_16;
					}
					else if ( currentWidth == 32) {
						uint32_t *src_32 = (uint32_t *)src;

						if ( dataWidth == 32 )
							fprintf(stderr, "%08x\n", src_32[0]);
						else if ( dataWidth == 64 )
							fprintf(stderr, "%016x\n", src_32[0]);

						src = ++src_32;
					}
					else if ( currentWidth == 64 ) {
						uint64_t *src_64 = (uint64_t *)src;

						if ( dataWidth == 64 )
							fprintf(stderr, "%016lx\n", src_64[0]);

						src = ++src_64;
					}
					else
						assert(0 && "can not handle this datawidth");
				}
			}
		}
		else { // array of struct
//			fprintf(stderr, "Array Of Struct : numofstructs : %d\n", numOfElements);

			for ( n=0; n < numOfElements; n++ )
			{
				for ( i=2; obj[i] != 0; )
				{
					int currentWidth = obj[i];
					i++;
					int currentNum = obj[i];
					i++;

					for ( j=0; j < currentNum; j++ )
					{
						if ( currentWidth == 2 ) { // pointer width is considered as 32bits in hls
							int **src_p = (int **)src;

							if ( dataWidth == 2 )
								fprintf(stderr, "PPPPPPPP\n");
							else if ( dataWidth == 32 )
								fprintf(stderr, "PPPPPPPP\n");
							else if ( dataWidth == 64 )
								fprintf(stderr, "PPPPPPPPPPPPPPPP\n");

							src = ++src_p;
						}
						else if ( currentWidth == 8 ) {
							uint8_t *src_8 = (uint8_t *)src;

							if ( dataWidth == 8 )
								fprintf(stderr, "%02x\n", src_8[0]);
							else if ( dataWidth == 16 )
								fprintf(stderr, "%04x\n", src_8[0]);
							else if ( dataWidth == 32 )
								fprintf(stderr, "%08x\n", src_8[0]);
							else if ( dataWidth == 64 )
								fprintf(stderr, "%016x\n", src_8[0]);

							src = ++src_8;
						}
						else if ( currentWidth == 16 ) {
							uint16_t *src_16 = (uint16_t *)src;

							if ( dataWidth == 16 )
								fprintf(stderr, "%04x\n", src_16[0]);
							else if ( dataWidth == 32 )
								fprintf(stderr, "%08x\n", src_16[0]);
							else if ( dataWidth == 64 )
								fprintf(stderr, "%016x\n", src_16[0]);

							src = ++src_16;
						}
						else if ( currentWidth == 32) {
							uint32_t *src_32 = (uint32_t *)src;

							if ( dataWidth == 32 )
								fprintf(stderr, "%08x\n", src_32[0]);
							else if ( dataWidth == 64 )
								fprintf(stderr, "%016x\n", src_32[0]);

							src = ++src_32;
						}
						else if ( currentWidth == 64 ) {
							uint64_t *src_64 = (uint64_t *)src;

							if ( dataWidth == 64 )
								fprintf(stderr, "%016lx\n", src_64[0]);

							src = ++src_64;
						}
						else
							assert(0 && "can not handle this datawidth");
					}

				}
			}
		}
	}
	else if ( isStruct && memBitWidth != 0 ) { // struct + membitwidth
//		fprintf(stderr, "Struct ID : %d\n", id);

		int accumulatedBits = 0;

		int *obj = id2struct[id];

		if ( obj[0] != 0 ) { // not array of struct

			for ( i=0; obj[i] != 0; )
			{
				int currentWidth = obj[i];
				i++;
				int currentNum = obj[i];
				i++;

//				fprintf(stderr, "width %d\n", currentWidth);
//				fprintf(stderr, "num %d\n", currentNum);
				for ( j=0; j < currentNum; j++ )
				{
					if ( currentWidth == 2 ) { // pointer width is considered as 32bits in hls
						int **src_p = (int **)src;
						fprintf(stderr, "PPPPPPPP");
						src = ++src_p;
					}
					else if ( currentWidth == 8 ) {
						uint8_t *src_8 = (uint8_t *)src;
						fprintf(stderr, "%02x", src_8[0]);
						src = ++src_8;
					}
					else if ( currentWidth == 16 ) {
						uint16_t *src_16 = (uint16_t *)src;
						fprintf(stderr, "%02x", (uint8_t)((src_16[0]) & MASK16));
						fprintf(stderr, "%02x", (uint8_t)((src_16[0] >> 8) & MASK16));
						src = ++src_16;
					}
					else if ( currentWidth == 32) {
						uint32_t *src_32 = (uint32_t *)src;
						fprintf(stderr, "%02x", (uint8_t)((src_32[0]) & MASK32));
						fprintf(stderr, "%02x", (uint8_t)((src_32[0] >> 8) & MASK32));
						fprintf(stderr, "%02x", (uint8_t)((src_32[0] >> 16) & MASK32));
						fprintf(stderr, "%02x", (uint8_t)((src_32[0] >> 24) & MASK32));
						src = ++src_32;
					}
					else if ( currentWidth == 64 ) {
						uint64_t *src_64 = (uint64_t *)src;
						fprintf(stderr, "%02x", (uint8_t)((src_64[0]) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 8) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 16) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 24) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 32) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 40) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 48) & MASK64));
						fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 56) & MASK64));
						src = ++src_64;
					}
					else
						assert(0 && "can not handle this datawidth");

					if ( currentWidth == 2 )
						accumulatedBits += 32;
					else
						accumulatedBits += currentWidth;

					if ( accumulatedBits % memBitWidth == 0 )
						fprintf(stderr, "\n");
				}
			}
			for ( k=0; k < (memBitWidth - (accumulatedBits % memBitWidth))/8; k++ )
				fprintf(stderr, "%02X", 0);

			if ( k!=0 )
				fprintf(stderr, "\n");
		}
		else { // array of struct
			// in this case the numOfElements will be the num of structs
			fprintf(stderr, "Array Of Struct : numofstructs : %d\n", numOfElements);

			for ( n=0; n < numOfElements; n++ )
			{
				for ( i=2; obj[i] != 0; )
				{
					int currentWidth = obj[i];
					i++;
					int currentNum = obj[i];
					i++;

//					fprintf(stderr, "width %d\n", currentWidth);
//					fprintf(stderr, "num %d\n", currentNum);
					for ( j=0; j < currentNum; j++ )
					{
						if ( currentWidth == 2 ) { // pointer width is considered as 32bits in hls
							int **src_p = (int **)src;
							fprintf(stderr, "PPPPPPPP");
							src = ++src_p;
						}
						else if ( currentWidth == 8 ) {
							uint8_t *src_8 = (uint8_t *)src;
							fprintf(stderr, "%02x", src_8[0]);
							src = ++src_8;
						}
						else if ( currentWidth == 16 ) {
							uint16_t *src_16 = (uint16_t *)src;
							fprintf(stderr, "%02x", (uint8_t)((src_16[0]) & MASK16));
							fprintf(stderr, "%02x", (uint8_t)((src_16[0] >> 8) & MASK16));
							src = ++src_16;
						}
						else if ( currentWidth == 32) {
							uint32_t *src_32 = (uint32_t *)src;
							fprintf(stderr, "%02x", (uint8_t)((src_32[0]) & MASK32));
							fprintf(stderr, "%02x", (uint8_t)((src_32[0] >> 8) & MASK32));
							fprintf(stderr, "%02x", (uint8_t)((src_32[0] >> 16) & MASK32));
							fprintf(stderr, "%02x", (uint8_t)((src_32[0] >> 24) & MASK32));
							src = ++src_32;
						}
						else if ( currentWidth == 64 ) {
							uint64_t *src_64 = (uint64_t *)src;
							fprintf(stderr, "%02x", (uint8_t)((src_64[0]) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 8) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 16) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 24) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 32) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 40) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 48) & MASK64));
							fprintf(stderr, "%02x", (uint8_t)((src_64[0] >> 56) & MASK64));
							src = ++src_64;
						}
						else
							assert(0 && "can not handle this datawidth");

						if ( currentWidth == 2 )
							accumulatedBits += 32;
						else
							accumulatedBits += currentWidth;

						if ( accumulatedBits % memBitWidth == 0 )
							fprintf(stderr, "\n");
					}
				}
			}

			for ( k=0; k < (memBitWidth - (accumulatedBits % memBitWidth))/8; k++ )
				fprintf(stderr, "%02X", 0);

			if ( k!=0 )
				fprintf(stderr, "\n");
		}
	}
	else { // no struct

		if ( memBitWidth == 0 ) {

			if ( dataWidth == 2 ) {
				for ( i=0; i<numOfElements; i++ )
					fprintf(stderr, "PPPPPPPP\n");
			}
			else if ( dataWidth == 8 ) {
				uint8_t *src_8 = (uint8_t *)src;
				for ( i=0; i<numOfElements; i++ )
					fprintf(stderr, "%02x\n", src_8[i]);
			}
			else if ( dataWidth == 16 ) {
				uint16_t *src_16 = (uint16_t *)src;
				for ( i=0; i<numOfElements; i++ )
					fprintf(stderr, "%04x\n", src_16[i]);
			}
			else if ( dataWidth == 32) {
				uint32_t *src_32 = (uint32_t *)src;
				for ( i=0; i<numOfElements; i++ )
					fprintf(stderr, "%08x\n", src_32[i]);
			}
			else if ( dataWidth == 64 ) {
				uint64_t *src_64 = (uint64_t *)src;
				for ( i=0; i<numOfElements; i++ )
					fprintf(stderr, "%016lx\n", src_64[i]);
			}
			else
				assert(0 && "can not handle this datawidth");
		}
		else{
			assert( memBitWidth % dataWidth == 0 );

			int concatLevel = memBitWidth / dataWidth;//2

			for ( i=0; i<numOfElements; i++ )
			{
				if ( dataWidth == 2 ) {
					fprintf(stderr, "PPPPPPPP");
				}
				else if ( dataWidth == 8 ) {
					uint8_t *src_8 = (uint8_t *)src;
					fprintf(stderr, "%02x", src_8[i]);
				}
				else if ( dataWidth == 16 ) {
					uint16_t *src_16 = (uint16_t *)src;
					fprintf(stderr, "%02x", (uint8_t)((src_16[i]) & MASK16));
					fprintf(stderr, "%02x", (uint8_t)((src_16[i] >> 8) & MASK16));
				}
				else if ( dataWidth == 32) {
					uint32_t *src_32 = (uint32_t *)src;
					fprintf(stderr, "%02x", (uint8_t)((src_32[i]) & MASK32));
					fprintf(stderr, "%02x", (uint8_t)((src_32[i] >> 8) & MASK32));
					fprintf(stderr, "%02x", (uint8_t)((src_32[i] >> 16) & MASK32));
					fprintf(stderr, "%02x", (uint8_t)((src_32[i] >> 24) & MASK32));
				}
				else if ( dataWidth == 64 ) {
					uint64_t *src_64 = (uint64_t *)src;
					fprintf(stderr, "%02x", (uint8_t)((src_64[i]) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 8) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 16) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 24) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 32) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 40) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 48) & MASK64));
					fprintf(stderr, "%02x", (uint8_t)((src_64[i] >> 56) & MASK64));
				}
				else
					assert(0 && "can not handle this datawidth");

				if ( (i+1) % concatLevel == 0 )
					fprintf(stderr, "\n");
			}

			for ( ; i % concatLevel != 0; i++ ) 
				for ( j = 0; j < dataWidth; j=j+8 )
					fprintf(stderr, "%02X", 0);

			if ( i != numOfElements )
				fprintf(stderr, "\n");
		}
	}

	fprintf(stderr, "\n");
}


extern "C" 
void printEnd (char *name)
{
	fprintf(stderr, "Function %s End\n\n", name);
}

extern "C" 
void registerStruct (int id)
{
	int *obj = (int *)malloc( 1000 * sizeof(int) );
	id2struct[id] = obj;
}

extern "C" 
void startStruct (void)
{
	offset = 0;
}

extern "C" 
void addElement (int id, int dataWidth, int num)
{
	int *obj = id2struct[id];
	obj[offset] = dataWidth;
	offset++;
	obj[offset] = num;
	offset++;
}

