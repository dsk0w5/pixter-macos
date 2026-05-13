#define _GNU_SOURCE
#include "util.h"
#include "disasm.h"
#include <stdarg.h>
#include <string.h>


struct OrdedListHeader {
	struct OrdedListHeader *next, *prev;
	uint32_t addr;
};

struct Comment {
	struct OrdedListHeader hdr;
	char str[];
};

struct Name {
	struct OrdedListHeader hdr;
	char name[];
};

struct Xref {
	struct OrdedListHeader hdr;
	uint32_t from;
	enum FlowRef flow;
};

static struct OrdedListHeader *mComments, *mXrefs, *mNames;
static uint8_t mFlags[ROM_MAX_SIZE] = {};
static DisasmCodeReadByteF mReadF;



void utilInit(DisasmCodeReadByteF readF)
{
	mReadF = readF;
}

bool disPrvGetU8(uint32_t addr, uint8_t *valP)
{
	int16_t ret = mReadF(addr);

	if (valP)
		*valP = ret;

	return ret >= 0;
}

bool disPrvGetU16(uint32_t addr, uint16_t *valP)
{
	uint8_t hi, lo;

	if (!disPrvGetU8(addr, &lo) || !disPrvGetU8(addr + 1, &hi)) {
		*valP = -1;
		return false;
	}

	if (valP)
		*valP = (((uint_fast16_t)hi) << 8) + lo;
	return true;
}

bool disPrvGetU32(uint32_t addr, uint32_t *valP)
{
	uint8_t hi, midhi, midlo, lo;

	if (!disPrvGetU8(addr, &lo) || !disPrvGetU8(addr + 1, &midlo) || !disPrvGetU8(addr + 2, &midhi) || !disPrvGetU8(addr + 3, &hi)) {
		*valP = -1;
		return false;
	}

	if (valP)
		*valP = (((uint32_t)hi) << 24) + (((uint32_t)midhi) << 16) + (((uint32_t)midlo) << 8) + lo;
	return true;
}

void disPrvSetType(uint32_t addr, uint_fast8_t type)		//does not set flags
{
	uint_fast8_t curType;

	if (addr < ROM_BASE || addr - ROM_BASE >= sizeof(mFlags) / sizeof(*mFlags))
		FATAL("cannot set type of invalid address 0x%04x\n", addr);
	addr -= ROM_BASE;

	type &= FLAG_MASK_TYPE;
	curType = mFlags[addr] & FLAG_MASK_TYPE;

	if (curType == type)	//acceptable to set same type, do not clear flasg then
		return;

	if (curType == FLAG_TYPE_UNEXPLORED) {	//acceptable to set type of unexplored fields, clears flags
		mFlags[addr] = type;
		return;
	}

	FATAL("cannot set type of address 0x%04x to 0x%02x,since it has type 0x%02x\n", addr, type, mFlags[addr]);
}

uint_fast8_t disPrvGetType(uint32_t addr)
{
	if (addr < ROM_BASE || addr - ROM_BASE >= sizeof(mFlags) / sizeof(*mFlags))
		FATAL("cannot get type of invalid address 0x%04x\n", addr);
	addr -= ROM_BASE;

	return mFlags[addr] & FLAG_MASK_TYPE;
}

void disPrvSetFlags(uint32_t addr, uint_fast8_t expectedType, uint_fast8_t flags)
{
	uint_fast8_t curType;

	expectedType &= FLAG_MASK_TYPE;
	
	if (addr < ROM_BASE || addr - ROM_BASE >= sizeof(mFlags) / sizeof(*mFlags))
		FATAL("cannot set flags of invalid address 0x%04x\n", addr);
	addr -= ROM_BASE;

	if (expectedType == FLAG_TYPE_UNEXPLORED)
		FATAL("cannot set flags of unexplored address 0x%04x\n", addr);

	curType = mFlags[addr] & FLAG_MASK_TYPE;
	if (curType != expectedType)
		FATAL("cannot set flags of address 0x%04x since it has type 0x%02x, but type 0x%02x was expeced\n", addr, curType, expectedType);

	mFlags[addr] = (mFlags[addr] &~ FLAG_MASK_FLAGS) | (flags & FLAG_MASK_FLAGS);
}

uint_fast8_t disPrvGetFlags(uint32_t addr, uint_fast8_t expectedType)
{
	uint_fast8_t curType;

	expectedType &= FLAG_MASK_TYPE;
	if (addr < ROM_BASE || addr - ROM_BASE >= sizeof(mFlags) / sizeof(*mFlags))
		FATAL("cannot get flags of invalid address 0x%04x\n", addr);
	addr -= ROM_BASE;

	if (expectedType == FLAG_TYPE_UNEXPLORED)
		FATAL("gannot get flags of unexplored address 0x%04x\n", addr);

	curType = mFlags[addr] & FLAG_MASK_TYPE;
	if (curType != expectedType)
		FATAL("cannot get flags of address 0x%04x since it has type 0x%02x, but type 0x%02x was expeced\n", addr, curType, expectedType);

	return mFlags[addr] & FLAG_MASK_FLAGS;
}

static void disPrvAddOrderedItem(struct OrdedListHeader **headP, struct OrdedListHeader *item)
{
	if (!*headP || (*headP)->addr >= item->addr) {
		item->prev = NULL;
		if ((item->next = (*headP)) != NULL)
			item->next->prev = item;
		(*headP) = item;
	}
	else {

		struct OrdedListHeader *prev = *headP, *cur = prev->next;

		while (cur && cur->addr < item->addr) {
			prev = cur;
			cur = cur->next;
		}

		//now prev is the item after which we should be, cur is item before which we should be (or NULL if we are the new last item)
		item->prev = prev;
		prev->next = item;

		if ((item->next = cur) != NULL)
			item->next->prev = item;
	}
}

static struct OrdedListHeader* disPrvNextOrderedItem(struct OrdedListHeader *head, struct OrdedListHeader *prevItem, uint32_t addr)
{
	struct OrdedListHeader *t;

	if (prevItem) {
		if (prevItem->next && prevItem->next->addr == addr)
			return prevItem->next;
		return NULL;
	}

	for (t = head; t && t->addr < addr; t = t->next);

	return (t && t->addr == addr) ? t : NULL;
}

void disPrvComment(uint32_t addr, const char *fmt, ...)
{
	struct Comment *cmt;
	va_list vl;
	char *str;
	int len;

	va_start(vl, fmt);
	len = vasprintf(&str, fmt, vl);
	va_end(vl);
	
	if (len < 0)
		return;

	cmt = malloc(sizeof(*cmt) + len + 1);
	if (cmt) {
		cmt->hdr.addr = addr;
		strcpy(cmt->str, str);

		disPrvAddOrderedItem(&mComments, &cmt->hdr);
	}
	free(str);
}

const char* disPrvGetGetCommentText(const struct Comment* comment)
{
	return comment->str;
}

struct Comment* disPrvGetNextCommentForAddr(struct Comment* prevComment, uint32_t addr)
{
	return (struct Comment*)disPrvNextOrderedItem(mComments, &prevComment->hdr, addr);
}

void disPrvXref(uint32_t from, uint32_t to, enum FlowRef kind)
{
	struct Xref *xref = malloc(sizeof(*xref));

	if (xref) {
		xref->from = from;
		xref->flow = kind;
		xref->hdr.addr = to;

		disPrvAddOrderedItem(&mXrefs, &xref->hdr);
	}
}

struct Xref* disPrvGetNextXrefForAddr(struct Xref* prevXref, uint32_t addr)
{
	return (struct Xref*)disPrvNextOrderedItem(mXrefs, &prevXref->hdr, addr);
}

void disPrvGetGetXrefInfo(const struct Xref* xref, uint32_t *fromP, enum FlowRef *flowKindP)
{
	if (fromP)
		*fromP = xref->from;
	if (flowKindP)
		*flowKindP = xref->flow;
}

bool disPrvName(uint32_t addr, const char *fmt, ...)
{
	if (disPrvGetName(addr)) {

		return false;
	}
	else {

		struct Name *name;
		va_list vl;
		char *str;
		int len;

		va_start(vl, fmt);
		len = vasprintf(&str, fmt, vl);
		va_end(vl);
		
		if (len < 0)
			return false;

		name = malloc(sizeof(*name) + len + 1);
		if (!name) {
			free(str);
			return false;
		}

		name->hdr.addr = addr;
		strcpy(name->name, str);
		free(str);

		disPrvAddOrderedItem(&mNames, &name->hdr);

		return true;
	}
}

const char* disPrvGetName(uint32_t addr)
{
	struct Name *name = (struct Name*)disPrvNextOrderedItem(mNames, NULL, addr);

	return name ? name->name : NULL;
}






