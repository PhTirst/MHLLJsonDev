#pragma once

// Minimal RenderWare declarations needed by MHLL.
// This is not a full RW SDK replacement. MHLL jumps to Manhunt's internal
// RenderWare functions, so the build only needs this narrow ABI surface.

typedef char RwChar;
typedef int RwBool;

struct RwRaster;
struct RwTexDictionary;

struct RwLLLink
{
	RwLLLink *next;
	RwLLLink *prev;
};

#define rwTEXTUREBASENAMELENGTH 32

struct RwTexture
{
	RwRaster *raster;
	RwTexDictionary *dict;
	RwLLLink lInDictionary;
	RwChar name[rwTEXTUREBASENAMELENGTH];
	RwChar mask[rwTEXTUREBASENAMELENGTH];
	unsigned int filterAddressing;
	int refCount;
};

#define RwTextureGetName(_tex) ((_tex)->name)

RwBool RwTexDictionaryDestroy(RwTexDictionary *dict);
RwTexture *RwTexDictionaryFindNamedTexture(RwTexDictionary *dict, const RwChar *name);
