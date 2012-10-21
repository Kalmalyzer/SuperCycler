
#ifndef PARSEIFF_H
#define PARSEIFF_H

#include "Types.h"

typedef bool (*IffChunkHandlerFunc)(void* state, void* buffer, unsigned int size);

typedef void (*IffErrorFunc)(const char* message);

typedef struct
{
	uint32_t id;
	IffChunkHandlerFunc handlerFunc;
} IffChunkHandler;

typedef struct
{
	IffErrorFunc errorFunc;
	IffChunkHandler* chunkHandlers;
	void* chunkHandlerState;
} IffParseRules;

enum
{
	ID_FORM = 'FORM',
	ID_ILBM = 'ILBM',
	ID_BMHD = 'BMHD',
	ID_BODY = 'BODY',
	ID_CMAP = 'CMAP',
	ID_CAMG = 'CAMG',
	ID_CRNG = 'CRNG',
};

bool parseIff(const char* fileName, const IffParseRules* rules);

#endif
