#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "disasm.h"

enum FlowRef {
	RefTypeCodeToData,
	RefTypeDataToCode,
	RefTypeDataToData,
	RefTypeCall,
	RefTypeUncondBr,
	RefTypeCondBr,
};

#define FATAL(...)		do {fprintf(stderr, __VA_ARGS__); abort(); } while(0)
#define WARNING(...)	do {fprintf(stderr, __VA_ARGS__); } while(0)

#define FLAG_MASK_TYPE					0x0F
#define FLAG_TYPE_UNEXPLORED			0x00
#define FLAG_TYPE_CODE					0x01
#define FLAG_TYPE_DATA					0x02	//bytes that are bytes
#define FLAG_TYPE_OFFSET				0x03	//bytes that are pointers

#define FLAG_MASK_FLAGS					0xc0
#define FLAG_CODE_MID_INSTR				0x80	//set for bytes that are NOT a start of an instruction
#define FLAG_DATA_U16_START				0x40	//for type "data": this is first byte of a u16
#define FLAG_DATA_U32_START				0x80	//for type "data": this is first byte of a u24


//init
void utilInit(DisasmCodeReadByteF readF);

//access to source bytes
bool disPrvGetU8(uint32_t addr, uint8_t *valP);
bool disPrvGetU16(uint32_t addr, uint16_t *valP);
bool disPrvGetU32(uint32_t addr, uint32_t *valP);

//type and flag access for disasm
void disPrvSetType(uint32_t addr, uint_fast8_t type);
uint_fast8_t disPrvGetType(uint32_t addr);
void disPrvSetFlags(uint32_t addr, uint_fast8_t expectedType, uint_fast8_t flags);
uint_fast8_t disPrvGetFlags(uint32_t addr, uint_fast8_t expectedType);

//comments
struct Comment;	//opaque
void disPrvComment(uint32_t addr, const char *fmt, ...);
struct Comment* disPrvGetNextCommentForAddr(struct Comment* prevComment, uint32_t addr);	//iterate comments for addr, pass NULL first time, do not intermix with adding comments
const char* disPrvGetGetCommentText(const struct Comment* comment);

//XREFS
struct Xref;	//opaque
void disPrvXref(uint32_t from, uint32_t to, enum FlowRef kind);
struct Xref* disPrvGetNextXrefForAddr(struct Xref* prevXref, uint32_t addr);				//iterate xrefs to addr, pass NULL first time, do not intermix with adding xrefs
void disPrvGetGetXrefInfo(const struct Xref* xref, uint32_t *fromP, enum FlowRef *flowKindP);

//NAMES
bool disPrvName(uint32_t addr, const char *fmt, ...);
const char* disPrvGetName(uint32_t addr);

#endif

