#include "pch.h"
#include <ntddk.h>

#include "SysMon.h"

Globals g_Globals;

extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING) {

	auto status = STATUS_SUCCESS;

	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.Mutex.Init();

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\sysmon");
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\sysmon");
	bool symLinkCreated = false, processCallbacks = false, threadCallbacks = false;

	PDEVICE_OBJECT DeviceObject = nullptr;
	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, true, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		dbgprintf("Failed to create device object.\n");
		goto end;
	}
	DeviceObject->Flags |= DO_DIRECT_IO;

	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		dbgprintf("Failed to create symbolic link.\n");
		goto end;
	}
	symLinkCreated = true;

	status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, false);
	if (!NT_SUCCESS(status)) {
		dbgprintf("Failed to set create process notify routine.\n");
		goto end;
	}
	processCallbacks = true;

	status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
	if (!NT_SUCCESS(status)) {
		dbgprintf("Failed to set create thread notify routine.\n");
		goto end;
	}
	threadCallbacks = false;

	status = PsSetLoadImageNotifyRoutine(OnLoadImageNotify);
	if (!NT_SUCCESS(status)) {
		dbgprintf("Failed to set load image notify routine.\n");
		goto end;
	}

end:
	if (!NT_SUCCESS(status)) {
		if (symLinkCreated)
			IoDeleteSymbolicLink(&symLink);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
		if (processCallbacks)
			PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, true);
		if (threadCallbacks)
			PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	}

	DriverObject->DriverUnload = SysMonUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = SysMonCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = SysMonRead;

	return status;

}

void SysMonUnload(PDRIVER_OBJECT DriverObject) {
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\sysmon");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	while (!IsListEmpty(&g_Globals.ItemsHead)) {
		auto entry = RemoveHeadList(&g_Globals.ItemsHead);
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
	}
}

NTSTATUS SysMonCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, 0);
	return STATUS_SUCCESS;
}

void PushItem(LIST_ENTRY* entry) {
	AutoLock<FastMutex> lock(g_Globals.Mutex);
	//implement this limit using the registry
	if (g_Globals.ItemCount > 1024) {
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		g_Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ItemsHead, entry);
	g_Globals.ItemCount++;
}

_Use_decl_annotations_
VOID OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);

	if (CreateInfo) {
		auto allocSize = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandLineSize = 0;
		if (CreateInfo->CommandLine) {
			commandLineSize = CreateInfo->CommandLine->Length;
			// allocate enough space for the command line text aswell
			allocSize += commandLineSize;
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (info == nullptr) {
			dbgprintf("Failed to allocate pool with tag on process create notify.\n");
			return;
		}

		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.Size = sizeof(ProcessCreateInfo) + commandLineSize;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);

		if (commandLineSize > 0) {
			memcpy((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer, commandLineSize);
			item.CommandLineLength = commandLineSize / sizeof(WCHAR);
			item.CommandLineOffset = sizeof(item);
		}
		else {
			item.CommandLineLength = 0;
		}
		PushItem(&info->Entry);
	}
	else {
		// process exited
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr) {
			dbgprintf("Failed to allocate pool with tag on process exit notify.\n");
			return;
		}

		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.Size = sizeof(ProcessExitInfo);

		PushItem(&info->Entry);
	}
}

_Use_decl_annotations_
VOID OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create) {
	
	// Remote thread creation check
	// OnThreadNotify is called in the context of the process that created the thread. So we can compare that process with the ProcessId that is passed to OnThreadNotify.
	auto currProcessId = PsGetCurrentProcessId();
	if (currProcessId == ProcessId) {
		auto size = sizeof(FullItem<ThreadCreateExitInfo>);
		auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
		if (info == nullptr) {
			dbgprintf("Failed to allocate pool with tag on thread notify.\n");
			return;
		}

		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.ThreadId = HandleToULong(ThreadId);
		item.Size = sizeof(ProcessExitInfo);

		PushItem(&info->Entry);
	}
	else {
		auto size = sizeof(FullItem<RemoteThreadCreateInfo>);
		auto info = (FullItem<RemoteThreadCreateInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
		if (info == nullptr) {
			dbgprintf("Failed to allocate pool with tag on remote thread notify.\n");
			return;
		}

		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::RemoteThreadCreate;
		item.TargetProcessId = HandleToULong(ProcessId);
		item.SourceProcessId = HandleToULong(currProcessId);	
		item.ThreadId = HandleToULong(ThreadId);
		item.Size = sizeof(RemoteThreadCreateInfo);

		PushItem(&info->Entry);
	}
}

_Use_decl_annotations_
VOID OnLoadImageNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo) {
	if (ImageInfo->SystemModeImage)
		return;

	auto size = sizeof(FullItem<ImageLoadInfo>);
	USHORT fullImageNameSize = 0;
	if (FullImageName != nullptr) {
		fullImageNameSize = FullImageName->Length;
		size += fullImageNameSize;
	}

	auto info = (FullItem<ImageLoadInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr) {
		dbgprintf("Failed to allocate pool with tag on image load notify.\n");
		return;
	}

	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Type = ItemType::ImageLoad;
	item.ProcessId = HandleToULong(ProcessId);
	item.Size = sizeof(ImageLoadInfo) + fullImageNameSize;

	if (fullImageNameSize) {
		memcpy((UCHAR*)&item + sizeof(item), FullImageName->Buffer, fullImageNameSize);
		item.ImagePathLength = fullImageNameSize / sizeof(WCHAR);
		item.ImagePathOffset = sizeof(item);
	}
	else {
		item.ImagePathLength = 0;
	}

	PushItem(&info->Entry);
}


NTSTATUS SysMonRead(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto count = 0;
	NT_ASSERT(Irp->MdlAddress);

	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer) {
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else {
		AutoLock lock(g_Globals.Mutex);
		while (true) {
			if (IsListEmpty(&g_Globals.ItemsHead))
				break;

			auto entry = RemoveHeadList(&g_Globals.ItemsHead);
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = info->Data.Size;
			if (len < size) {
				InsertHeadList(&g_Globals.ItemsHead, entry);
				break;
			}
			g_Globals.ItemCount--;
			memcpy(buffer, &info->Data, size);
			len -= size;
			buffer += size;
			count += size;
			ExFreePool(info);
		}
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, 0);
	return status;
}