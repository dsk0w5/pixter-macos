#ifndef LZ_H
#define LZ_H


#include <stdbool.h>
#include <stdint.h>

//this lz comprssion differs from perious we had - variable length numbers are encoded BE here for decompression sped


struct LzState {		//reaondly after init

	void *readD;							//init these
	uint32_t inPosStart, inPosPastEnd;		//init these
	uint8_t *dst, marker;					//init dst

	uint_fast8_t (*lzReadByteF)(uint32_t ofst, void *userData);
};


bool lzUncompressStart(struct LzState *state);							//call this once
bool lzDecompressSome(struct LzState *state, uint32_t approxMax);		//decompress some data, at least max unless there isnt that much, return true if there may be more to decompress


uint32_t lzCompress(const uint8_t* inbV, uint8_t* outV, uint32_t insize, uint32_t blockSz);



#endif