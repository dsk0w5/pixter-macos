#include <stdlib.h>
#include "lz.h"

#pragma GCC optimize ("Ofast")

struct HashEntry {
	struct HashEntry *next;
	uint32_t ofst;
};


static int_fast16_t lzPrvReadNextByte(struct LzState *state)		//negative on out of data
{
	if (state->inPosStart >= state->inPosPastEnd)
		return -1;

	return (uint_fast16_t)state->lzReadByteF(state->inPosStart++, state->readD);
}

static int32_t lzPrvRead(struct LzState *state)
{
	uint32_t ret = 0;
	int32_t t;

	while ((t = lzPrvReadNextByte(state)) >= 0) {

		ret = ret << 7;
		ret += t & 0x7f;
		if (!(t & 0x80))
			return ret;
	}

	return -1;
}

bool lzUncompressStart(struct LzState *state)
{
	int_fast16_t t;

	t = lzPrvReadNextByte(state);
	if (t < 0)
		return false;
	state->marker = t;

	t = lzPrvReadNextByte(state);
	if (t < 0)
		return false;
	*state->dst++ = t;
	
	return true;
}

bool lzDecompressSome(struct LzState *state, uint32_t approxMax)
{
	uint8_t *dstStart = state->dst;
	int_fast16_t t;

	while ((t = lzPrvReadNextByte(state)) >= 0) {

		if (t != state->marker) {		//raw byte? copy it
			
			*state->dst++ = t;
		}
		else {
			int32_t len, offset = lzPrvRead(state);

			if (offset < 0)
				return false;

			if (offset == 0) {	//emit a single marker byte

				*state->dst++ = state->marker;
			}
			else {

				len = lzPrvRead(state);
				if (len < 0)
					return false;

				len += 3;

	#ifdef __arm__
				uint32_t dummy1, dummy2;

				asm (

					"	cmp		%2, #8					\n\t"
					"	bcc		1f						\n\t"		//short copy -> bytewise
					"	orr		%0, %3, %4				\n\t"
					"	movs	%0, %0, LSL #31			\n\t"
					"	bne		1f						\n\t"		//bytewise
					"	bcs		2f						\n\t"		//halfwordwise

					//wordwise (we get here with at least 8 bytes to copy for sure)
					"	sub		%2, #4					\n\t"
					"8:									\n\t"
					"	ldr		%0, [%3, -%4]			\n\t"
					"	subs	%2, #4					\n\t"
					"	str		%0, [%3], #4			\n\t"
					"	bpl		8b						\n\t"
					"	adds	%2, #4					\n\t"
					"	bne		1f						\n\t"		//more to copy - go bytewise
					"	b		9f						\n\t"		//nomore to copy - go to end

					//halfwordwise (we get here with at least 8 bytes to copy for sure)
					"2:									\n\t"
					"	sub		%2, #2					\n\t"
					"8:									\n\t"
					"	ldrh	%0, [%3, -%4]			\n\t"
					"	subs	%2, #2					\n\t"
					"	strh	%0, [%3], #2			\n\t"
					"	bpl		8b						\n\t"
					"	adds	%2, #2					\n\t"
					"	beq		9f						\n\t"		//no more to copy - go to end

					//bytewise (we get here with at least 1 byte to copy for sure)
					"1:									\n\t"
					"	ldrb	%0, [%3, -%4]			\n\t"
					"	subs	%2, #1					\n\t"
					"	strb	%0, [%3], #1			\n\t"
					"	bne		1b						\n\t"

					//done
					"9:									\n\t"

					: "=&r"(dummy1), "=&r"(dummy2), "+r"(len), "+r"(state->dst)
					: "r"(offset)
					:"memory", "cc"
				);
	#else
				 while (len--) {
					state->dst[0] = state->dst[-offset];
					state->dst++;
				}
	#endif
			}

		}

		if ((uint32_t)(state->dst - dstStart) >= approxMax)
			return true;
	}
	return false;
}




#define LZ_NUM_SETS	256
#define LZ_NUM_WAYS	4

static uint32_t fnvHash(const uint8_t *data)
{
	uint32_t ret = 0x177;
	
	ret ^= *data++;
	ret *= 0x01000193;
	
	ret ^= *data++;
	ret *= 0x01000193;
	
	ret ^= *data++;
	ret *= 0x01000193;
	
	ret ^= *data++;
	ret *= 0x01000193;

	return ret;
}

static uint32_t lzHashCollapse(uint32_t raw)
{
	raw ^= raw >> 10;
	raw ^= raw >> 10;
	
	return raw & 0xfff;
}

static uint_fast8_t lzWrite(uint32_t val, uint8_t *out)
{
	uint_fast8_t numBitsNeeded = val ? 32 - __builtin_clz(val) : 1;
	uint_fast8_t i, numBytesNeeded = (numBitsNeeded + 6) / 7;
	uint32_t shift = 7 * (numBytesNeeded - 1);

	for (i = 0; i < numBytesNeeded; i++, shift -= 7) {
		
		*out++ = ((val >> shift) & 0x7f) + (shift ? 0x80 : 0x00);	//all but th elast byte get top bit set
	}

	return numBytesNeeded;
}

uint32_t lzCompress(const uint8_t* inb, uint8_t* out, uint32_t insize, uint32_t blockSz)
{
	struct HashEntry *heads[4096] = {0,}, *hashes = calloc(sizeof(struct HashEntry), insize);	//could be big!
	uint32_t inpos = 0, lastHashed = 0, nextFreeHashEntry = 0;
	uint32_t i, histogram[256] = {0, }, outpos = 0;
	struct HashEntry *opt;
	uint8_t marker;

	// Do we have anything to compress?
	if (!insize)
		return 0;

	// Create histogram
	for (i = 0; i < insize; i++)
		histogram[inb[i]]++;

	// Find the least common byte, and use it as the code marker
	marker = 0;
	for (i = 1; i < 256; i++){
		if (histogram[i] < histogram[marker])
			marker = i;
	}

	out[outpos++] = marker;
	
	//we need to emit a single char, but it could be the marker, so handle that...
	if (inb[inpos++] == marker) {

		out[outpos++] = marker;
		out[outpos++] = 0;
	}
	else {
		out[outpos++] = inb[inpos - 1];
	}

	do {
		
		uint32_t bestLength = 0, bestOffset = 0;
		
		//hash new data
		while (lastHashed < inpos) {
			
			uint32_t hash = lzHashCollapse(fnvHash(inb + lastHashed));
			struct HashEntry **p = heads + hash;
			
			opt = hashes + nextFreeHashEntry++;
			opt->next = *p;
			*p = opt;
			opt->ofst = lastHashed;
			
			lastHashed++;
		}
		
		//hash-accelerated search
		for (opt = heads[lzHashCollapse(fnvHash(inb + inpos))]; opt; opt = opt->next) {
			
			uint32_t possibleOfst = opt->ofst;
			uint32_t maxMatchLen = insize - inpos;
			uint32_t matchLen = 0;
			
			//if we are too far back, do not bother (speed opt)
			if (inpos - possibleOfst > blockSz)
				break;
			
			//check for actual match (hash does not promise anything)
			for (matchLen = 0; matchLen < maxMatchLen && inb[possibleOfst + matchLen] == inb[inpos + matchLen]; matchLen++);
			
			//is it better than before ?
			if (matchLen > bestLength) {
				bestLength = matchLen;
				bestOffset = possibleOfst;
			}
		}
		
		//offset should be relative to cur pos
		bestOffset = inpos - bestOffset;
		
		// how big will the encoded backreference be?
		if (bestLength >= 3 /* never makes sense to do that, and we do not even have a way to encode that */) {
			
			uint32_t encodingSize = 1 + (32 - __builtin_clz(bestLength) + 6) / 7 + (32 - __builtin_clz(bestOffset) + 6) / 7;
		
			if (encodingSize < bestLength) {
				
				out[outpos++] = marker;
				outpos += lzWrite(bestOffset, out + outpos);
				outpos += lzWrite(bestLength - 3, out + outpos);
				inpos += bestLength;
				continue;
			}
		}
		
		if (inb[inpos] == marker) {	//raw marker byte? emit escape
			
			out[outpos++] = inb[inpos++];
			out[outpos++] = 0;
		}
		else {								//copy byte
			
			out[outpos++] = inb[inpos++];
		}
		
	} while(inpos < insize);

	return outpos;
}





