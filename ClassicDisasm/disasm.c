#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "disasm.h"
#include "util.h"



struct DisasmQueueItem {
	struct DisasmQueueItem *next;
	uint32_t from, addr;
};

struct DisasmQueueItem *mQueue;


static void disPrvOrrFlags(uint32_t addr, uint_fast8_t expectedType, uint_fast8_t flagsToOrrIn)
{
	disPrvSetFlags(addr, expectedType, flagsToOrrIn | disPrvGetFlags(addr, expectedType));
}

static bool disPrvVerifyByteAndMarkAndFlag(uint32_t addr, uint8_t expectedMask, uint_fast8_t expectedVal, uint_fast8_t typeAndFlags)
{
	uint8_t val;

	if (!disPrvGetU8(addr, &val) || (val ^ expectedVal) & expectedMask)
		return false;

	disPrvSetType(addr, typeAndFlags & FLAG_MASK_TYPE);
	disPrvSetFlags(addr, typeAndFlags & FLAG_MASK_TYPE, typeAndFlags & FLAG_MASK_FLAGS);

	return true;
}

static void disPrvMarkHeaderPtrU16(uint32_t addr)
{
	uint16_t dummy;

	//verify readable
	if (!disPrvGetU16(addr, &dummy))
		FATAL("Header word at 0x%05x not readable\n", addr);

	disPrvSetType(addr, FLAG_TYPE_OFFSET);
	disPrvSetType(addr + 1, FLAG_TYPE_OFFSET);
	disPrvSetFlags(addr, FLAG_TYPE_OFFSET, FLAG_DATA_U16_START);
}

static bool disPrvIsValidAddr(uint32_t addr)
{
	return addr >= FIRST_VALID_ADDR && addr <= LAST_VALID_ADDR;
}

static void disPrvEnqueue(uint32_t from, uint32_t addr, enum FlowRef flow)
{
	struct DisasmQueueItem *item = malloc(sizeof(*item));

	if (!disPrvIsValidAddr(addr))
		WARNING("Invalid branch from 0x%04x to 0x%04x\n", from, addr);
	else {
	
		disPrvXref(from, addr, flow);

		if (item) {
			item->from = from;
			item->addr = addr;
			item->next = mQueue;
			mQueue = item;
		}
	}
}

static void disPrvWalkBasicBlock(uint32_t addr)
{
	int_fast16_t lastPushVal = -1;	//probabilistic but seen often
	bool goOn = true;
	uint16_t hword;
	uint8_t byte;

	fprintf(stderr, "walking basic block 0x%04x\n", addr);

	while (goOn && disPrvGetU8(addr, &byte)) {

		uint_fast8_t extraLen = 0, prevType = disPrvGetType(addr);

		if (prevType != FLAG_TYPE_UNEXPLORED) {
			WARNING("stopping disasm at 0x%04x since non-explored data was encountered (type %u)\n", addr, prevType);
			break;
		}

		switch (byte) {
			case 0xe0:
				extraLen = 1;
				break;

			case 0xe1:
				extraLen = 2;
				break;

			case 0xe2:
				extraLen = 3;
				break;

			case 0xf0:		//bra
				if (!disPrvGetU16(addr + 1, &hword))
					WARNING("Cannot read %s destination at 0x%04x", "jump", addr);
				else
					disPrvEnqueue(addr, hword, RefTypeUncondBr);
				extraLen = 2;
				goOn = false;
				break;

			case 0xf1:		//bnez
			case 0xf2:		//beqz
				if (!disPrvGetU16(addr + 1, &hword))
					WARNING("Cannot read %s destination at 0x%04x", "condbr", addr);
				else
					disPrvEnqueue(addr, hword, RefTypeCondBr);
				extraLen = 2;
				if ((lastPushVal == 0 && byte == 0xf2) || (lastPushVal > 0 && byte == 0xf1)) {

					//many games use push followed by bnez for relative jumps, this is a heuristic since someone could jump to our consitional jump, but i consider that unlikely
					goOn = false;
				}
				break;
			
			case 0xf3:		//switchjump
				if (!disPrvGetU16(addr + 2, &hword))
					WARNING("Cannot read %s destination at 0x%04x", "switchbr", addr);
				else
					disPrvEnqueue(addr, hword, RefTypeCondBr);
				extraLen = 3;
				break;

			case 0xf4:		//call
				if (!disPrvGetU16(addr + 2, &hword))
					WARNING("Cannot read %s destination at 0x%04x", "call", addr);
				else
					disPrvEnqueue(addr, hword, RefTypeCall);
				extraLen = 3;
				break;

			case 0xf5:		//return
				extraLen = 1;
				goOn = false;
				break;
		}
		if (byte < 0x40) {

			lastPushVal = byte;
		}
		else if (byte == 0xe0) {

			disPrvGetU8(addr + 1, &byte);
			lastPushVal = (uint16_t)byte;
		}
		else {

			lastPushVal = -1;
		}

		disPrvSetType(addr++, FLAG_TYPE_CODE);
		while (extraLen--) {

			disPrvSetType(addr, FLAG_TYPE_CODE);
			disPrvOrrFlags(addr, FLAG_TYPE_CODE, FLAG_CODE_MID_INSTR);
			addr++;
		}
	}
}

static void disPrvConsiderBlock(uint32_t from, uint32_t addr)
{
	uint_fast8_t curType = disPrvGetType(addr);

	if (curType == FLAG_TYPE_CODE) {	//already explored
		
		if (disPrvGetFlags(addr, FLAG_TYPE_CODE) & FLAG_CODE_MID_INSTR)
			WARNING("ref to middle of instr 0x%04x -> 0x%04x", from, addr);
	}
	else if (curType != FLAG_TYPE_UNEXPLORED) {

		WARNING("Byte at 0x%04x is of type 0x%02x, so it will not be disassembled as code. Got here from 0x%04x\n", addr, curType, from);
	}
	else {
		
		disPrvWalkBasicBlock(addr);
	}
}

static bool disPrvGetLocName(char *dst, unsigned dstLen, uint32_t addr)		//eventualyl we'll add ability to name thngs, for now try to be smart
{
	struct Xref *xref = NULL;
	const char *userName;


	if (!disPrvIsValidAddr(addr))
		return false;

	//if we have a user-provided name, lways use that
	userName = disPrvGetName(addr);
	if (userName) {
		snprintf(dst, dstLen, "%s", userName);
		return true;
	}

	if (disPrvGetType(addr) != FLAG_TYPE_CODE)	//only code has names for now
		return false;

	if (disPrvGetFlags(addr, FLAG_TYPE_CODE) & FLAG_CODE_MID_INSTR) {
		WARNING("bytes in the middle of an instruction have no name");
		return false;
	}

	while ((xref = disPrvGetNextXrefForAddr(xref, addr)) != NULL) {

		enum FlowRef flowKind;

		disPrvGetGetXrefInfo(xref, NULL, &flowKind);

		if (flowKind == RefTypeDataToCode || flowKind == RefTypeCall) {
			snprintf(dst, dstLen, "sub_%06x", addr);
			return true;
		}
	}
	snprintf(dst, dstLen, "loc_%06x", addr);
	return true;
}

static uint32_t disPrvOneInstr(const uint32_t addr, const char **extraNewlinesP)
{
	static const char instrs[0xa0][14] = {
		/* 0x60 */	"NOP",				"IDLE_CHECK"	,	"NOP",				"DROP_TOP_2",		"SET_DOWNCNT",		"GET_DOWNCNT",		"RAND",				"RAND",
		/* 0x68 */	"???", 				"NOP",				"GET_PIX",			"SET_PIX",			"DRAW_BUF_0",		"DRAW_BUF_0",		"COPY_SCREEN",		"COPY_SCREEN",
		/* 0x70 */	"CLRMEM8",			"CLRMEM8",			"CLRMEM8",			"CLRMEM8",			"CLRMEM8",			"CLRMEM8",			"CLRMEM8",			"CLRMEM8",
		/* 0x78 */	"NOP",				"NOP",				"DROP_TOP_2",		"COPY_BUF",			"COPY_BUF",			"COPY_BUF",			"COPY_BUF",			"COPY_BUF",
		/* 0x80 */	"COPY_BUF",			"CLR_BUF",			"DROP_TOP",			"SET_VOLUME",		"GET_VOLUME",		"NOP",				"NOP",				"NOP",
		/* 0x88 */	"NOP",				"NOP",				"FLOOD_FILL",		"NOP",				"???",				"NOP",				"???",				"NOP",
		/* 0x90 */	"NOP",				"NOP",				"???",				"DRW_BUF_0_INV",	"DRW_BUF_0_INV",	"GET_PEN",			"SPECIAL_96",		"PLAY_JINGLE",
		/* 0x98 */	"ADPC_EXTD",		"IMG2ROM",			"IMG2ROM",			"ROM2IMG",			"PLAYCANNED",		"GET_PIN_F6",		"AUD_WAITEND",		"SPECIAL_9F",
		/* 0xA0 */	"DROP_TOP_3",		"DROP_TOP_3",		"DROP_TOP_3",		"DROP_TOP_2",		"DROP_2_UNDR_1",	"???",				"SET_SCENE_IDX",	"???",
		/* 0xA8 */	"DRAW_IMAGE",		"SCROLL_W_BUF", 	"SCROLL_W_BUF", 	"SCROLL_SIMPLE",	"???", 				"PLAY_ADPCM",		"PLAY_ADPCM", 		"GET_ADPCM_STA",
		/* 0xB0 */	"MEMWR8",			"NOT8",				"MEMWR16",			"NOT16",			"MEMpreinc8",		"MEMpredec8", 		"MEMpostinc8",		"MEMpostdec8",
		/* 0xB8 */	"MEMpreinc16",		"MEMpredec16", 		"MEMpostinc16",		"MEMpostdec16",		"ADD24",			"SUB24",			"MEMWR24",			"STRINGLIT",
		/* 0xC0 */	"AND8",				"ORR8",				"EOR8",				"UMOD8",			"ADD8",				"SUB8",				"MUL8",				"UDIV8",
		/* 0xC8 */	"IS_EQ8",			"IS_NE8",			"IS_GT8",			"IS_GE8",			"IS_LT8",			"IS_LE8",			"LSL8",				"LSR8",
		/* 0xD0 */	"AND16",			"ORR16",			"EOR16",			"UMOD16",			"ADD16",			"SUB16",			"MUL16",			"UDIV16",
		/* 0xD8 */	"IS_EQ16",			"IS_NE16",			"IS_GT16",			"IS_GE16",			"IS_LT16",			"IS_LE16",			"LSL16",			"LSR16",
		/* 0xE0 */	"CONST8",			"CONST16",			"CONST24",			"MEMRD8",			"MEMRD16",			"MEMRD24",			"ADDRGEN_8x1",		"ADDRGEN_8x2",
		/* 0xE8 */	"ADDRGEN_8x3",		"ROMRD8",			"ROMRD16",			"ROMRD24",			"ADDRGEN_24x1",		"ADDRGEN_24x2",		"ADDRGEN_24x3",		"ROMWR8",
		/* 0xF0 */	"BRA",				"BNEZ",				"BEQZ",				"SWITCHJMP",		"CALL",				"RETURN",			"U8_TO_16",			"U8_to_24",
		/* 0xF8 */	"U16_TO_8",			"U16_TO_24",		"U24_TO_8",			"U24_TO_16",		"NOP",				"DROP_TOP_3",		"DROP_TOP_2",		"DROP_TOP",
	};

	char disasm[256], locName[128];
	uint_fast8_t i, len = 1;
	uint8_t byte, instr;
	uint32_t word24;
	uint16_t hword;

	if (!disPrvGetU8(addr, &instr))
		sprintf(disasm, "???");
	else if (instr < 0x40)
		sprintf(disasm, "PUSHIMM8      0x%02x", instr);
	else if (instr < 0x50)
		sprintf(disasm, "MEMRD8        [0x%02x]", instr - 0x40);
	else if (instr < 0x60)
		sprintf(disasm, "MEMWR8        [0x%02x]", instr - 0x50);
	else if (instr == 0xe0) {

		if (disPrvGetU8(addr + 1, &byte))
			sprintf(disasm, "PUSHIMM8      0x%02x", byte);
		else
			sprintf(disasm, "PUSHIMM8      ??");
		len = 2;
	}
	else if (instr == 0xe1) {

		if (disPrvGetU16(addr + 1, &hword))
			sprintf(disasm, "PUSHIMM16     0x%04x", hword);
		else
			sprintf(disasm, "PUSHIMM16     ??");
		len = 3;
	}
	else if (instr == 0xe2) {

		if (disPrvGetU24(addr + 1, &word24))
			sprintf(disasm, "PUSHIMM24     0x%04x", word24);
		else
			sprintf(disasm, "PUSHIMM24     ??");
		len = 4;
	}
	else if (instr == 0xBF) {

		FATAL("0xBF seen - srting literal\n");
	}
	else if (instr >= 0xf0 && instr <= 0xf4) {

		static const char *names[] = {"BRA      ", "BNEZ     ", "BEQZ     ", "SWITCHJMP", "CALL     "};
		bool gotExtraByte = false, gotDest;
		uint32_t addrAt = addr + 1;
		
		if (instr >= 0xf3) {

			gotExtraByte = disPrvGetU8(addr + 1, &byte);
			len++;
			addrAt++;
		}
		gotDest = disPrvGetU16(addrAt, &hword);
		len += 2;
		
		if (!gotDest)
			sprintf(locName, "????");
		else if (!disPrvGetLocName(locName, sizeof(locName), hword))
			sprintf(locName, "0x%04x", hword);
		
		if (instr < 0xf3)
			sprintf(disasm, "%s     %s", names[instr - 0xf0], locName);
		else if (!gotExtraByte)
			sprintf(disasm, "%s     ??, %s", names[instr - 0xf0], locName);
		else
			sprintf(disasm, (instr == 0xf3) ? "%s     0x%02x, %s" : "%s     %u, %s", names[instr - 0xf0], byte, locName);
	}
	else if (instr == 0xf5) {
		if (disPrvGetU8(addr + 1, &byte))
			sprintf(disasm, "RET           %u", byte);
		else
			sprintf(disasm, "RET           ??");
		len = 2;
		*extraNewlinesP = "\n\n";
	}
	else {

		strcpy(disasm, instrs[instr - 0x60]);
	}
	
	while (strlen(disasm) < 20)
		strcat(disasm, " ");

	//longest possible instr is 4 bytes
	for (i = 0; i < len; i++) {
		if (disPrvGetU8(addr + i, &byte))
			printf("%02x ", byte);
		else
			printf("?? ");
	}
	for(;i < 4; i++) {
		printf("   ");
	}
	printf(" %s", disasm);

	return len;
}

void disasm(DisasmCodeReadByteF readF)
{
	uint32_t addrAddend, addr;
	uint16_t initialPC;

	utilInit(readF);

	//mark known header locations
	if (!disPrvVerifyByteAndMarkAndFlag(0xbf00, 0xff, 0xaa, FLAG_TYPE_DATA | FLAG_DATA_U16_START) || !disPrvVerifyByteAndMarkAndFlag(0xbf01, 0xff, 0x55, FLAG_TYPE_DATA))
		FATAL("MAGIC 1 mismatch\n");
	
	disPrvComment(0xbf00, "MAGIC");

	//second magic byte which can be 0xaa or 0xff
	if (!disPrvVerifyByteAndMarkAndFlag(0xbf11, 0xaa, 0xaa, FLAG_TYPE_DATA))
		FATAL("MAGIC 2 mismatch\n");
	
	disPrvComment(0xbf11, "MAGIC 2");

	if (!disPrvGetU16(0xbf06, &initialPC) || !disPrvIsValidAddr(initialPC))
		FATAL("initial PC value of 0x%04x is invalid\n", initialPC);
	disPrvMarkHeaderPtrU16(0xbf06);
	disPrvComment(0xbf06, "initial pc");
	
	disPrvMarkHeaderPtrU16(0xbf04);
	disPrvComment(0xbf04, "image scene list addr");
	
	disPrvMarkHeaderPtrU16(0xbf08);
	disPrvComment(0xbf08, "audio offset");
	
	disPrvMarkHeaderPtrU16(0xbf0a);
	disPrvComment(0xbf0a, "object list addr");
	
	disPrvName(initialPC, "__START");
	disPrvEnqueue(0xbf06, initialPC, RefTypeDataToCode);
	while (mQueue) {
		struct DisasmQueueItem *item = mQueue;

		mQueue = item->next;
		disPrvConsiderBlock(item->from, item->addr);
		free(item);
	}

	//print
	for (addr = FIRST_VALID_ADDR; addr <= LAST_VALID_ADDR; addr += addrAddend) {

		const char missingStrFull[] = "??????", *missingStr, *typeName, *fmt, *extraNewlines = NULL;
		uint_fast8_t type = disPrvGetType(addr), flags;
		struct Comment *cmt = NULL;
		bool stillSameLine = true;
		struct Xref *xref = NULL;
		char locName[128];
		uint32_t word24;
		uint16_t hword;
		uint8_t byte;
		bool gotVal;

		//if there are xrefs, print a name
		if (disPrvGetNextXrefForAddr(xref, addr) && disPrvGetLocName(locName, sizeof(locName), addr))
			printf("%06x: %s:\n", addr, locName);

		printf("%06x:   ", addr);

		addrAddend = 1;
		switch (type) {
			case FLAG_TYPE_UNEXPLORED:
				if (disPrvGetU8(addr, &byte))
					printf(".byte 0x%02x", byte);
				else
					printf(".byte ??");
				break;

			case FLAG_TYPE_OFFSET:
			case FLAG_TYPE_DATA:
				flags = disPrvGetFlags(addr, type);
				if (flags & FLAG_DATA_U16_START) {

					missingStr = missingStrFull + 2;
					typeName = ".hword";
					fmt = "0x%04x";
					gotVal = disPrvGetU16(addr, &hword);
					addrAddend = 2;
					word24 = hword;
				}
				else if (flags & FLAG_DATA_U24_START) {

					missingStr = missingStrFull + 0;
					typeName = ".word24";
					fmt = "0x%06x";
					gotVal = disPrvGetU24(addr, &word24);
					addrAddend = 3;
				}
				else {
					
					missingStr = missingStrFull + 4;
					typeName = ".byte";
					fmt = "0x%02x";
					gotVal = disPrvGetU8(addr, &byte);
					word24 = byte;
				}

				printf("%s ", typeName);
				
				if (!gotVal)
					printf("%s%s", (type == FLAG_TYPE_OFFSET) ? "loc_" : "", missingStr);
				else if (type != FLAG_TYPE_OFFSET || !disPrvGetLocName(locName, sizeof(locName), word24)) {

					printf(fmt, word24);
				}
				else {

					printf("%s (", locName);
					printf(fmt, word24);
					printf(")");
				}
				break;

			case FLAG_TYPE_CODE:
				addrAddend = disPrvOneInstr(addr, &extraNewlines);
				break;
		}
		
		//comments
		while ((cmt = disPrvGetNextCommentForAddr(cmt, addr)) != NULL) {

			printf("%s;%s\n", (stillSameLine ? "\t" : "\t\t\t\t"), disPrvGetGetCommentText(cmt));
			stillSameLine = false;
		}

		//xrefs
		while ((xref = disPrvGetNextXrefForAddr(xref, addr)) != NULL) {
			static const char *flowName[] = {
				[RefTypeCodeToData] = "DREF",
				[RefTypeDataToCode] = "CODEPTR",
				[RefTypeDataToData] = "DATAPTR",
				[RefTypeCall] = "CALL",
				[RefTypeUncondBr] = "BRANCH",
				[RefTypeCondBr] = "BRANCH",
			};

			enum FlowRef flowKind;
			uint32_t from;

			disPrvGetGetXrefInfo(xref, &from, &flowKind);
			if (!disPrvGetLocName(locName, sizeof(locName), from))
				sprintf(locName, "0x%04x", from);

			printf("%s;%s from %s\n", (stillSameLine ? "\t" : "\t\t\t\t"), flowName[flowKind], locName);
			stillSameLine = false;
		}

		if (stillSameLine)
			printf("\n");

		if (extraNewlines)
			puts(extraNewlines);
	}
}







