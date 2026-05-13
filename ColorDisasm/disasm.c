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

static void disPrvMarkHeaderUxx(uint32_t addr, bool isPtr, uint32_t numBytes, uint32_t flag)
{
	uint32_t dummy, type = isPtr ? FLAG_TYPE_OFFSET : FLAG_TYPE_DATA;

	//verify readable
	if (!disPrvGetU32(addr, &dummy))
		FATAL("Header word at 0x%08x not readable\n", addr);

	for (dummy = 0; dummy < numBytes; dummy++)
		disPrvSetType(addr + dummy, type);

	disPrvSetFlags(addr, type, flag);
}

static void disPrvMarkHeaderU32(uint32_t addr, bool isPtr)
{
	disPrvMarkHeaderUxx(addr, isPtr, 4, FLAG_DATA_U32_START);
}

static void disPrvMarkHeaderU16(uint32_t addr)
{
	disPrvMarkHeaderUxx(addr, false, 2, FLAG_DATA_U16_START);
}

static bool disPrvIsValidAddr(uint32_t addr)
{
	return addr >= ROM_BASE && (addr - ROM_BASE) < ROM_MAX_SIZE;
}

static void disPrvEnqueue(uint32_t from, uint32_t addr, enum FlowRef flow)
{
	struct DisasmQueueItem *item = malloc(sizeof(*item));

	if (!disPrvIsValidAddr(addr))
		WARNING("Invalid branch from 0x%08x to 0x%08x\n", from, addr);
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

static bool disPrvGetCallDest(uint32_t ofst, uint32_t *dstP)
{
	uint32_t constants;

	if (!disPrvGetU32(ROM_BASE + ROM_OFST_CONSTANTS, &constants)) {

		WARNING("Cannot read CONSTANTS at 0x%04x\n", ROM_BASE + ROM_OFST_CONSTANTS);
		return false;
	}

	if (dstP)
		*dstP =  constants + 2 * ofst;
	return true;
}

static void disPrvWalkBasicBlock(uint32_t addr)
{
	bool goOn = true;
	uint16_t hword;
	uint32_t word;
	int32_t prevPushVal = -1;		//actually lower 16 bits of val, if that matters

	fprintf(stderr, "walking basic block 0x%04x\n", addr);

	while (goOn && disPrvGetU16(addr, &hword)) {

		uint_fast8_t extraLen = 1 /* second byte of instr */, prevType = disPrvGetType(addr);
		int32_t thisPushVal = -1;

		if (prevType != FLAG_TYPE_UNEXPLORED) {
			WARNING("stopping disasm at 0x%04x since non-explored data was encountered (type %u)\n", addr, prevType);
			break;
		}

		if (hword < 0x0200) {			//format a
			
			thisPushVal = (uint32_t)(hword & 0x1ff);

			if ((hword & 0x1ff) == 0x1ff) {
				extraLen += 2;
				thisPushVal = disPrvGetU16(addr + 2, &hword) ? (uint32_t)hword : -1;
			}
		}
		else if (hword < 0x2000) {		//format b
			
			if ((hword & 0xff) == 0xff)
				extraLen += 2;
		}
		else if (hword < 0x2800) {		//format c
			
			if ((hword & 0xff) == 0xff)
				extraLen += 2;
		}
		else if (hword < 0x2900) {		//format d
			
			uint_fast8_t ofst = 2;

			if ((hword & 0xff) == 0xff) {
				ofst += 2;
				extraLen += 2;
			}

			thisPushVal = disPrvGetU16(addr + ofst, &hword) ? (uint32_t)hword : -1;
			extraLen += 2;
		}
		else if (hword < 0x2980) {		//format e
			
			if (hword >= 0x2978 && hword <= 0x297d) {	// RET0 .. RET3
				
				goOn = false;
			}
			else if (hword == 0x297f) {		//this native call uses an extra word

				extraLen += 2;
			}
		}
		else if (hword < 0x3000) {		//format f
			
			if ((hword & 0x1f) == 0x1f)
				extraLen += 2;
		}
		else if (hword < 0xa000) {		//format g
		
			//nothing
		}
		else if (hword < 0xb000) {		//format h
		
			//nothing
		}
		else if (hword < 0xc000) {		//format j
			
			if ((hword & 0x0fff) == 0x0fff)
				extraLen += 2;
		}
		else if (hword < 0xe000) {		//format k
			
			int32_t ofst = hword & 0x1fff;

			extraLen += 2;
			if (ofst == 0x1fff) {

				if (!disPrvGetU16(addr + 2, &hword)) {
					ofst = -1;
					WARNING("Cannot read call extension word at 0x%08x\n", addr);
				}
				else {

					ofst = (uint32_t)hword;
				}
				extraLen += 2;
			}

			if (ofst >= 0 && disPrvGetCallDest(ofst, &word))
				disPrvEnqueue(addr, word, RefTypeCall);
		}
		else if (hword < 0xf800) {		//format l
			
			bool unconditional = false;
			int32_t offset;
			uint32_t dst;

			if (hword & 0x0400)
				offset = (0xfffff800 | hword) - 1;
			else
				offset = hword & 0x3ff;

			dst = addr + 2 + 2 * offset;

			switch ((hword >> 11) & 3) {
				case 0:	//bra
					unconditional = true;
					break;

				case 1:	//branch if odd
					unconditional = (prevPushVal >= 0 && (prevPushVal & 1));
					break;

				case 2:	//branch if even
					unconditional = (prevPushVal >= 0 && !(prevPushVal & 1));
					break;
			}

			if (unconditional) {
				
				disPrvEnqueue(addr, dst, RefTypeUncondBr);
				goOn = false;
			}
			else {	

				disPrvEnqueue(addr, dst, RefTypeCondBr);
			}
		}
		else {							//format m
			
			disPrvEnqueue(addr, addr + 4 + 2 * (hword & 0x7ff), RefTypeCondBr);
			extraLen += 2;
		}

		disPrvSetType(addr++, FLAG_TYPE_CODE);
		while (extraLen--) {

			disPrvSetType(addr, FLAG_TYPE_CODE);
			disPrvOrrFlags(addr, FLAG_TYPE_CODE, FLAG_CODE_MID_INSTR);
			addr++;
		}

		prevPushVal = thisPushVal;
	}
}

static void disPrvConsiderBlock(uint32_t from, uint32_t addr)
{
	uint_fast8_t curType = disPrvGetType(addr);

	if (curType == FLAG_TYPE_CODE) {	//already explored
		
		if (disPrvGetFlags(addr, FLAG_TYPE_CODE) & FLAG_CODE_MID_INSTR)
			WARNING("ref to middle of instr 0x%08x -> 0x%08x\n", from, addr);
	}
	else if (curType != FLAG_TYPE_UNEXPLORED) {

		WARNING("Byte at 0x%08x is of type 0x%02x, so it will not be disassembled as code. Got here from 0x%08x\n", addr, curType, from);
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
		WARNING("bytes in the middle of an instruction have no name (0x%08x requested)\n", addr);
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

static bool disPrvGetExtdImm(uint32_t addr, uint_fast8_t *ofstP, uint16_t hword, uint_fast8_t nBits, uint32_t *valP)
{
	uint32_t mask = (1 << nBits) - 1;
	
	hword &= mask;

	if (hword == mask) {

		bool success = disPrvGetU16(addr + *ofstP, &hword);

		(*ofstP) += 2;
		if (!success) {
			WARNING("Cannot read imm at 0x%08x\n", addr + *ofstP);
			return false;
		}
	}
	
	if (valP)
		*valP = hword;
	
	return true;
}

static uint32_t disPrvOneInstr(const uint32_t addr, const char **extraNewlinesP)
{
	//these occur in the same order always
	static const char stdOps[][6] = {"AND  ", "ORR  ", "EOR  ", "UMOD ", "ADD  ", "SUB  ", "MUL16", "UDIV ", "IS_EQ", "IS_NE", "IS_GT", "IS_GE", "IS_LT", "IS_LE", };

	char disasm[384], locName[128];
	uint_fast8_t i, len = 2;
	uint16_t hword;
	uint32_t word;

	if (!disPrvGetU16(addr, &hword)) {

		sprintf(disasm, "???");
	}
	else if (hword < 0x0200) {			//format a
		
		if (!disPrvGetExtdImm(addr, &len, hword, 9, &word)) {

			sprintf(disasm, "PUSHIMM      #??????");
		}
		else {

			sprintf(disasm, "PUSHIMM      #0x%04x", word);
		}
	}
	else if (hword < 0x2000) {		//format b
		
		uint_fast8_t op = (hword - 0x0200) >> 8;

		if (!disPrvGetExtdImm(addr, &len, hword, 8, &word)) {

			word = 0xffffffff;
		}

		if (op < 14) {
			if (word == 0xffffffff)
				sprintf(disasm, "%s        push, pop, #??????", stdOps[op]);
			else
				sprintf(disasm, "%s        push, pop, #0x%04x", stdOps[op], word);
		}
		else if (op < 16) {
			if (word == 0xffffffff)
				sprintf(disasm, "%cDW          MEM32[#???????]", (op == 14) ? 'L' : 'S');
			else
				sprintf(disasm, "%cDW          MEM32[#0x%05x]", (op == 14) ? 'L' : 'S', word);
		}
		else {
			if (word == 0xffffffff)
				sprintf(disasm, "%s        push, pop, MEM16[#???????]", stdOps[op - 16]);
			else
				sprintf(disasm, "%s        push, pop, MEM16[#0x%05x]", stdOps[op - 16], word * 2);
		}
	}
	else if (hword < 0x2800) {		//format c
		
		if (!disPrvGetExtdImm(addr, &len, hword, 8, &word)) {

			word = 0xffffffff;
		}

		sprintf(disasm, "%sH          ", (hword & 0x0400) ? "ST" : "LD");
		switch ((hword >> 8) & 3) {
			case 0:
				if (word == 0xffffffff)
					sprintf(disasm + strlen(disasm), "MEM16[#?????]");
				else
					sprintf(disasm + strlen(disasm), "MEM16[#0x%05x]", word * 2);
				break;

			case 1:
				if (word == 0xffffffff)
					sprintf(disasm + strlen(disasm), "MEM16[#????? + pop() * 2]");
				else
					sprintf(disasm + strlen(disasm), "MEM16[#0x%05x + pop() * 2]", word * 2);
				break;

			case 2:
				if (word == 0xffffffff)
					sprintf(disasm + strlen(disasm), "MEM16[MEM16[#?????] * 2]");
				else
					sprintf(disasm + strlen(disasm), "MEM16[MEM16[#0x%05x] * 2]", word * 2);
				break;

			case 3:
				if (word == 0xffffffff)
					sprintf(disasm + strlen(disasm), "MEM16[MEM16[#????? + pop() * 2] * 2]");
				else
					sprintf(disasm + strlen(disasm), "MEM16[MEM16[#0x%05x + pop() * 2] * 2]", word * 2);
				break;
		}
	}
	else if (hword < 0x2900) {		//format d
		
		if (!disPrvGetExtdImm(addr, &len, hword, 8, &word)) {

			word = 0xffffffff;
		}
		else if (!disPrvGetU16(addr + len, &hword)) {

			word = 0xffffffff;
			WARNING("Cannot read imm at 0x%08x\n", addr + len);
		}
		else {

			word = (word << 16) + hword;
		}
		
		if (word == 0xffffffff)
			sprintf(disasm, "PUSHLONG     #????????");
		else
			sprintf(disasm, "PUSHLONG     #0x%08x", word);

		len += 2;
	}
	else if (hword < 0x2980) {		//format e
		
		static const char *miscOps[] = {
			[0x04] = "GET_PEN",
			[0x08] = "SAVE_FILE_PIECE",
			[0x09] = "FLSH_RD_4000",
			[0x0a] = "PLAU_AUDIO",
			[0x0b] = "GET_MISC_SETTING",
			[0x0c] = "SET_GLOBAL_4_to_8_CLUT",
			[0x0d] = "GETPIX",
			[0x0e] = "SCROLLBUF",
			[0x10] = "PEEK",
			[0x11] = "POKE",
			[0x14] = "BUFFER_LOGICAL_OP",
			[0x15] = "DRAW_OBJ_IMAGE",
			[0x18] = "DRAW_CIRCLE",
			[0x19] = "DRAW_LINE",
			[0x1a] = "DRAW_RECT_FRAME",
			[0x1b] = "SET_UI_SETTING",
			[0x1c] = "DRAW_PIX_AND_SET_DIRTY_BUF",
			[0x1d] = "DRAW_LINE_CONTINUE",
			[0x1e] = "GET_PEN_2",
			[0x1f] = "MANUAL_CALIBRATE",
			[0x21] = "FLOOD_FILL",
			[0x22] = "UI_PROCESS_TAP",
			[0x27] = "PUTCHAR",
			[0x28] = "SET_TEXT_POS",
			[0x29] = "SET_TEXT_FONT",
			[0x2a] = "CR_SUB_IMG",
			[0x2b] = "SET_TWO_BUFFERS",
			[0x2c] = "MAGIC",
			[0x2d] = "BUFCPY",
			[0x2e] = "DRAW_RAM_IMAGE",
			[0x30] = "RESET",
			[0x31] = "SLEEP",
			[0x32] = "RDTIME",
			[0x33] = "RSTTIME",
			[0x38] = "LSR8UXTB",
			[0x39] = "UXTB",
			[0x3a] = "RAND",
			[0x3b] = "SET_CLUT_37",
			[0x3c] = "NTV_CALL_28",
			[0x3d] = "GET_BATT_ADC",
			[0x41] = "FLSH_PRG_RAW",
			[0x42] = "FLSH_READ_RAW",
			[0x43] = "FLSH_ERZ_RAW",
			[0x44] = "FLSH_READ_RAWBUF",
			[0x45] = "FLSH_PRG_RAWBUF",
			[0x46] = "BUFSEL",
			[0x47] = "BUFWR",
			[0x48] = "BUFRD",
			[0x49] = "GETPAL",
			[0x4a] = "SETPAL",
			[0x4c] = "FLSH_WR_A400",
			[0x4d] = "FLSH_WR_B400",
			[0x4e] = "FLSH_RD_A400",
			[0x4f] = "FLSH_RD_B400",
			[0x51] = "SETCONTRAST",
			[0x52] = "BUFCLR",
			[0x53] = "GWT_HW_REV",
			[0x54] = "WRDOWNCOUNT",
			[0x55] = "RDDOWNCOUNT",
			[0x56] = "MASKEDIMGADJ",
			[0x57] = "SPRITECOPY",
			[0x58] = "SETIMGADJCLUTMAX",

			[0x60] = "AND",
			[0x61] = "ORR",
			[0x62] = "EOR",
			[0x63] = "UMOD",
			[0x64] = "ADD",
			[0x65] = "SUB",
			[0x66] = "MUL",
			[0x67] = "UDIV",
			[0x68] = "IS_EQ",
			[0x69] = "IS_NE",
			[0x6a] = "IS_GT",
			[0x6b] = "IS_GE",
			[0x6c] = "IS_LT",
			[0x6d] = "IS_LE",
			[0x6e] = "LSL",
			[0x6f] = "LSR",
			[0x70] = "NOT",
			[0x71] = "NOTq",
			[0x72] = "ADDq",
			[0x73] = "SUBq",

			[0x78] = "RET          0",
			[0x79] = "RET          1",
			[0x7a] = "RET          2",
			[0x7b] = "RET          3",

			[0x7e] = "DROP_TOP",
		};

		if (hword == 0x297f) {
			
			if (!disPrvGetU16(addr + len, &hword)) {

				sprintf(disasm, "NTV_CALL_2C  #?????");
				WARNING("Cannot read imm at 0x%08x\n", addr + len);
			}
			else {

				sprintf(disasm, "NTV_CALL_2C  #0x%04x", hword);
			}
			len += 2;
		}
		else if (hword - 0x2900 < sizeof(miscOps) / sizeof(*miscOps) && miscOps[hword - 0x2900]) {

			strcpy(disasm, miscOps[hword - 0x2900]);

			if (hword >= 0x2978 && hword <= 0x297b)
				*extraNewlinesP = "\n\n";
		}
		else {

			sprintf(disasm, "UNK_INSTR_0x%04x", hword);
		}
	}
	else if (hword < 0x3000) {		//format f
		
		static const char opsPre[4][3] = {"++", "--", "", ""};
		static const char opsPost[4][3] = {"", "", "++", "--"};
		uint8_t op = ((hword >> 5) & 0x3f) - 0x0c;

		if (!disPrvGetExtdImm(addr, &len, hword, 5, &word))
			strcpy(locName, "#???????");
		else
			sprintf(locName, "#0x%05x", word * 2);

		if (op < 4) {

			sprintf(disasm, "PUSH         %sMEM16[%s]%s", opsPre[op], locName, opsPost[op]);
		}
		else if (op < 12) {

			sprintf(disasm, "%s        MEM16[%s], MEM16[%s], pop", stdOps[op - 4], locName, locName);
		}
		else if (op == 12) {

			sprintf(disasm, "PUSH         (&LOCAL_VARS16[%s] -  &MEM16[0]) / 2", locName);
		}
		else if (op == 13) {

			sprintf(disasm, "PUSH         (&LOCAL_VARS16[%s] -  &MEM16[0]) / 2 + pop()", locName);
		}
		else if (op < 28) {

			sprintf(disasm, "%s        push, pop, LOCAL_VARS16[%s]", stdOps[op - 14], locName);
		}
		else if (op < 36) {

			op -= 28;

			switch (op % 4) {
				case 0:
					sprintf(disasm, "%s_LOCAL     LOCAL_VARS16[%s]", (op / 4) ? "WR" : "RD", locName);
					break;
				
				case 1:
					sprintf(disasm, "%s_LOCAL     LOCAL_VARS16[%s + pop() * 2]", (op / 4) ? "WR" : "RD", locName);
					break;

				case 2:
					sprintf(disasm, "%s_LOCAL     MEM16[LOCAL_VARS16[%s] * 2]", (op / 4) ? "WR" : "RD", locName);
					break;

				case 3:
					sprintf(disasm, "%s_LOCAL     MEM16[LOCAL_VARS16[%s] * 2 + pop() * 2]", (op / 4) ? "WR" : "RD", locName);
					break;
			}
		}
		else if (op < 38) {

			op -= 36;

			sprintf(disasm, "%sLOCALW     LOCAL_VARS32[%s]", op ? "WR" : "RD", locName);
		}
		else if (op < 40) {

			sprintf(disasm, "???????");
		}
		else if (op < 44) {

			sprintf(disasm, "PUSH         %sLOCAL_VARS16[%s]%s", opsPre[op - 40], locName, opsPost[op - 40]);
		}
		else {

			sprintf(disasm, "%s        sLOCAL_VARS16[%s], sLOCAL_VARS16[%s], pop", stdOps[op - 44], locName, locName);
		}
	}
	else if (hword < 0xa000) {		//format g
		
		uint_fast8_t a = (hword & 0x0f), b = ((hword >> 4) & 0x0f);

		sprintf(disasm, "%s        push, ", stdOps[(hword >> 11) - 6]);
		
		switch ((hword >> 8) & 7) {
			case 0:
				sprintf(disasm + strlen(disasm), "#0x%02x, MEM16[#%02x]", b, a * 2);
				break;

			case 1:
				sprintf(disasm + strlen(disasm), "#0x%02x, LOCAL_VARS16[#%02x]", b, a * 2);
				break;

			case 2:
				sprintf(disasm + strlen(disasm), "MEM16[#%02x], #0x%02x", b * 2, a);
				break;

			case 3:
				sprintf(disasm + strlen(disasm), "MEM16[#%02x], MEM16[#%02x]", b * 2, a * 2);
				break;

			case 4:
				sprintf(disasm + strlen(disasm), "MEM16[#%02x], LOCAL_VARS16[#%02x]", b * 2, a * 2);
				break;

			case 5:
				sprintf(disasm + strlen(disasm), "LOCAL_VARS16[#%02x], #0x%02x", b * 2, a);
				break;

			case 6:
				sprintf(disasm + strlen(disasm), "LOCAL_VARS16[#%02x], MEM16[#%02x]", b * 2, a * 2);
				break;

			case 7:
				sprintf(disasm + strlen(disasm), "LOCAL_VARS16[#%02x], LOCAL_VARS16[#%02x]", b * 2, a * 2);
				break;
		}
	}
	else if (hword < 0xb000) {		//format h
	
		sprintf(disasm, "STRH         #0x%02x, %s16[#0x%02x]", hword & 0x3f, (hword & 0x0800) ? "LOCAL_VARS" : "MEM", (hword >> 5) & 0x3e);

	}
	else if (hword < 0xc000) {		//format j
		
		if (!disPrvGetExtdImm(addr, &len, hword, 12, &word))
			sprintf(disasm, "PUSHCONST    CONSTANTS16[#??????? + 2 * pop()]");
		else
			sprintf(disasm, "PUSHCONST    CONSTANTS16[#%05x + 2 * pop()]", word * 2);
	}
	else if (hword < 0xe000) {		//format k
		
		if (!disPrvGetExtdImm(addr, &len, hword, 12, &word))
			word = 0xffffffff;
		if (!disPrvGetU16(addr + len, &hword)) {

			word = 0xffffffff;
			WARNING("Cannot read imm at 0x%08x\n", addr + len);
		}

		if (word == 0xffffffff || !disPrvGetCallDest(word, &word))
			sprintf(disasm, "CALL         ????????????");
		else {

			if (!disPrvGetLocName(locName, sizeof(locName), word))
				sprintf(locName, "0x%08x", hword);

			sprintf(disasm, "CALL         nLoc = %u, nParams = %u, %s", (hword & 0xff), hword >> 8, locName);
		}
		len += 2;
	}
	else if (hword < 0xf800) {		//format l
		
		static const char branchTypes[3][5] = {"RA  ", "ODD ", "EVEN"};
		int32_t offset;

		if (hword & 0x0400)
			offset = (0xfffff800 | hword) - 1;
		else
			offset = hword & 0x3ff;

		word = addr + 2 + 2 * offset;

		if (!disPrvGetLocName(locName, sizeof(locName), word))
			sprintf(locName, "0x%08x", hword);

		sprintf(disasm, "B%s        %s", branchTypes[(hword >> 11) & 3], locName);
	}
	else {							//format m
		
		word = addr + 4 + 2 * (hword & 0x7ff);

		if (!disPrvGetU16(addr + len, &hword)) {

			WARNING("Cannot read imm at 0x%08x\n", addr + len);
			sprintf(disasm, "CASEJUMP     ????????");
		}
		else {

			if (!disPrvGetLocName(locName, sizeof(locName), word))
				sprintf(locName, "0x%08x", hword);

			sprintf(disasm, "CASEJUMP     #0x%04x, %s", hword, locName);
		}
		len += 2;
	}
	
	while (strlen(disasm) < 20)
		strcat(disasm, " ");

	//longest possible instr is 6 bytes
	for (i = 0; i < len; i += 2) {
		if (disPrvGetU16(addr + i, &hword))
			printf("%04x ", hword);
		else
			printf("???? ");
	}
	for(;i < 6; i += 2) {
		printf("     ");
	}
	printf(" %s", disasm);

	return len;
}

static uint32_t disPrvHeaderPtrWithName(uint32_t offset, const char *name)
{
	uint32_t addr;

	if (!disPrvGetU32(ROM_BASE + offset, &addr) || !disPrvIsValidAddr(addr))
		FATAL("%s value of 0x%08x is invalid\n", name, addr);
	disPrvMarkHeaderU32(ROM_BASE + offset, true);
	disPrvComment(ROM_BASE + offset, name);

	return addr;
}

static void disPrvPrintXrefs(uint32_t addr, bool *stillSameLineP)
{
	struct Xref *xref = NULL;
	char locName[128];

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

		printf("%s;%s from %s\n", (*stillSameLineP ? "\t" : "\t\t\t\t"), flowName[flowKind], locName);
		*stillSameLineP = false;
	}
}

void disasm(DisasmCodeReadByteF readF)
{
	uint32_t addrAddend, addr, initialPC;

	utilInit(readF);

	//mark known header locations
	if (!disPrvVerifyByteAndMarkAndFlag(ROM_BASE + 0, 0xff, 0xcc, FLAG_TYPE_DATA | FLAG_DATA_U32_START) ||
			!disPrvVerifyByteAndMarkAndFlag(ROM_BASE + 1, 0xff, 0x66, FLAG_TYPE_DATA) ||
			!disPrvVerifyByteAndMarkAndFlag(ROM_BASE + 2, 0xff, 0x55, FLAG_TYPE_DATA) ||
			!disPrvVerifyByteAndMarkAndFlag(ROM_BASE + 3, 0xff, 0xaa, FLAG_TYPE_DATA))
		FATAL("MAGIC 1 mismatch\n");
	
	disPrvComment(ROM_BASE + 0, "MAGIC");

	disPrvMarkHeaderU32(ROM_BASE + 0x04, false);
	disPrvComment(ROM_BASE + 0x04, "VERSION");

	disPrvHeaderPtrWithName(0x08, "LAYOUT_TAB");
	disPrvHeaderPtrWithName(0x0c, "CONSTANTS");
	initialPC = disPrvHeaderPtrWithName(0x10, "INITIAL_PC");
	disPrvHeaderPtrWithName(0x14, "MEMSZ_PTR");
	disPrvHeaderPtrWithName(0x18, "OBJECTS");
	disPrvHeaderPtrWithName(0x28, "CODE_28");
	disPrvHeaderPtrWithName(0x2C, "CODE_2C");

	disPrvName(initialPC, "__START");
	disPrvEnqueue(ROM_BASE + 0x10, initialPC, RefTypeDataToCode);
	while (mQueue) {
		struct DisasmQueueItem *item = mQueue;

		mQueue = item->next;
		disPrvConsiderBlock(item->from, item->addr);
		free(item);
	}

	//print
	for (addr = ROM_BASE; addr - ROM_BASE < ROM_MAX_SIZE && disPrvGetU8(addr, NULL); addr += addrAddend) {

		const char missingStrFull[] = "??????", *missingStr, *typeName, *fmt, *extraNewlines = NULL;
		uint_fast8_t type = disPrvGetType(addr), flags;
		bool gotVal, skipXrefsNow = false;
		struct Comment *cmt = NULL;
		bool stillSameLine = true;
		char locName[128];
		uint16_t hword;
		uint32_t word;
		uint8_t byte;

		//if there are xrefs, print a name, then the XREFS
		if (disPrvGetNextXrefForAddr(NULL, addr) && disPrvGetLocName(locName, sizeof(locName), addr)) {
			bool sameLine = true;

			printf("%06x: %s:", addr, locName);

			disPrvPrintXrefs(addr, &sameLine);
			if (sameLine)
				printf("\n");

			skipXrefsNow = true;
		}
		

		printf("%08x:   ", addr);

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
					word = hword;
				}
				else if (flags & FLAG_DATA_U32_START) {

					missingStr = missingStrFull + 0;
					typeName = ".word";
					fmt = "0x%08x";
					gotVal = disPrvGetU32(addr, &word);
					addrAddend = 4;
				}
				else {
					
					missingStr = missingStrFull + 4;
					typeName = ".byte";
					fmt = "0x%02x";
					gotVal = disPrvGetU8(addr, &byte);
					word = byte;
				}

				printf("%s ", typeName);
				
				if (!gotVal)
					printf("%s%s", (type == FLAG_TYPE_OFFSET) ? "loc_" : "", missingStr);
				else if (type != FLAG_TYPE_OFFSET || !disPrvGetLocName(locName, sizeof(locName), word)) {

					printf(fmt, word);
				}
				else {

					printf("%s (", locName);
					printf(fmt, word);
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
		if (!skipXrefsNow)
			disPrvPrintXrefs(addr, &stillSameLine);
		skipXrefsNow = false;

		if (stillSameLine)
			printf("\n");

		if (extraNewlines)
			puts(extraNewlines);
	}
}







