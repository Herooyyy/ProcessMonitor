#include "pch.h"

#include "SysMonCommon.h"
#include "SysSync.h"

#define dbgprintf(a, ...) DbgPrintEx(0, 0, "%s(): " a, __func__, ##__VA_ARGS__)

#define DRIVER_TAG 'nmys'

template<typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};


struct Globals {
public:
	LIST_ENTRY ItemsHead;
	int ItemCount;
	FastMutex Mutex;
};

// Function prototypes
VOID OnProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _In_ PPS_CREATE_NOTIFY_INFO CreateInfo);
VOID OnThreadNotify(_In_ HANDLE ProcessId, _In_ HANDLE ThreadId, _In_ BOOLEAN Create);
VOID OnLoadImageNotify(_In_opt_ PUNICODE_STRING FullImageName, _In_ HANDLE ProcessId, _In_ PIMAGE_INFO ImageInfo);

NTSTATUS SysMonRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS SysMonCreateClose(PDEVICE_OBJECT, PIRP Irp);
void SysMonUnload(PDRIVER_OBJECT DriverObject);