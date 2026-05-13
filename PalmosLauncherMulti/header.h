#ifndef _FILE_HEADER_H_
#define _FILE_HEADER_H_



#define MAGIC						0x27182818

//#define USE_BBUART

//our data starts at 0x4000 on page 0, and goes on from there to the end of the cart (1M minus 16KB of space, of which the first 16 bytes are used for a header)
//data begins with four BIG ENDIAN words:
//	u32 magic
//	u32 dstAddr;
//	u32 dstSize;
//	u32	compressedLen;


#endif

