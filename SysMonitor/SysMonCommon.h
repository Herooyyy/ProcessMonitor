#include "pch.h"

enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

struct ItemHeader {
	ItemType Type;
	USHORT Size;
	LARGE_INTEGER Time;
};


// Structs
struct ProcessExitInfo : ItemHeader {
	ULONG ProcessId;
};

struct ProcessCreateInfo : ItemHeader {
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
};

struct ThreadCreateExitInfo : ItemHeader {
	ULONG ProcessId;
	ULONG ThreadId;
};

struct ImageLoadInfo : ItemHeader {
	ULONG ProcessId;
	PVOID ImageBase;
	USHORT ImagePathLength;
	USHORT ImagePathOffset;
};