#include <ntddk.h>
#include <wdmsec.h>
#include <ntdddisk.h>
#include <initguid.h>
#include "xdvdfs_reader.h"

#pragma comment(lib, "wdmsec.lib")

#define XDVDFS_DEVICE_NAME  L"\\Device\\XboxXdvdfsReader"
#define XDVDFS_DOS_NAME     L"\\DosDevices\\XboxXdvdfsReader"
#define XDVDFS_CDROM_NAME   L"\\Device\\CdRom0"

DEFINE_GUID(GUID_DEVCLASS_XDVDFS_READER,
    0x7a4d2e31, 0x7f42, 0x4f19, 0x9a, 0x32, 0x21, 0x4b, 0x8d, 0x71, 0x60, 0x11);

static VOID CompleteIrp(PIRP Irp, NTSTATUS Status, ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    CompleteIrp(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

/* Read-only raw-sector bridge. This is not a mounted filesystem. */
static NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID systemBuffer = Irp->AssociatedIrp.SystemBuffer;
    XDVDFS_READ_REQUEST request;
    ULONGLONG byteOffset, byteCount64;
    LARGE_INTEGER offset;
    UNICODE_STRING deviceName;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    HANDLE deviceHandle = NULL;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(DeviceObject);

    if (code != IOCTL_XDVDFS_READ_SECTORS) {
        CompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (systemBuffer == NULL || inLength < sizeof(XDVDFS_READ_REQUEST)) {
        CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(&request, systemBuffer, sizeof(request));
    if (request.SectorCount == 0 || request.SectorCount > XDVDFS_MAX_SECTORS_PER_READ) {
        CompleteIrp(Irp, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }

    byteCount64 = (ULONGLONG)request.SectorCount * XDVDFS_SECTOR_SIZE;
    if (byteCount64 > MAXULONG || outLength < (ULONG)byteCount64) {
        CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (request.StartingSector > (MAXULONGLONG / XDVDFS_SECTOR_SIZE)) {
        CompleteIrp(Irp, STATUS_INTEGER_OVERFLOW, 0);
        return STATUS_INTEGER_OVERFLOW;
    }

    byteOffset = request.StartingSector * XDVDFS_SECTOR_SIZE;
    offset.QuadPart = (LONGLONG)byteOffset;
    RtlInitUnicodeString(&deviceName, XDVDFS_CDROM_NAME);
    InitializeObjectAttributes(&attributes, &deviceName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&deviceHandle, FILE_READ_DATA | SYNCHRONIZE,
        &attributes, &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) {
        CompleteIrp(Irp, status, 0);
        return status;
    }

    status = ZwReadFile(deviceHandle, NULL, NULL, NULL, &ioStatus,
        systemBuffer, (ULONG)byteCount64, &offset, NULL);
    ZwClose(deviceHandle);

    if (NT_SUCCESS(status)) {
        ULONG_PTR transferred = (ULONG_PTR)ioStatus.Information;
        if (transferred > (ULONG_PTR)byteCount64)
            transferred = (ULONG_PTR)byteCount64;
        CompleteIrp(Irp, status, transferred);
        return status;
    }
    CompleteIrp(Irp, status, 0);
    return status;
}

static VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING dosName;
    RtlInitUnicodeString(&dosName, XDVDFS_DOS_NAME);
    IoDeleteSymbolicLink(&dosName);
    if (DriverObject->DeviceObject != NULL)
        IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING deviceName, dosName, defaultSddl;
    UNREFERENCED_PARAMETER(RegistryPath);

    RtlInitUnicodeString(&deviceName, XDVDFS_DEVICE_NAME);
    RtlInitUnicodeString(&defaultSddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    status = IoCreateDeviceSecure(DriverObject, 0, &deviceName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
        &defaultSddl, &GUID_DEVCLASS_XDVDFS_READER, &deviceObject);
    if (!NT_SUCCESS(status))
        return status;

    RtlInitUnicodeString(&dosName, XDVDFS_DOS_NAME);
    status = IoCreateSymbolicLink(&dosName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->DriverUnload = DriverUnload;
    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
