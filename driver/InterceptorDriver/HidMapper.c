#include "Driver.h"
#include "HidInputBuffers.h"
#include "../Shared/PrepareStatus.h"
#include "../Shared/HidMapperProfile.h"
#include "../Shared/AxisCache.h"
#include "../Shared/HidKeyState.h"
#include "../Shared/MouseReportMode.h"
// HID mapper adapted from the local ConfigCSASoftwareWebV2 project.
static SHORT FilterScaleWheel(LONG value,BOOLEAN reverse) {
    const LONGLONG units=(LONGLONG)value*(reverse?-120:120);
    return (SHORT)max(-32768,min(32767,units));
}




static void FilterStartLocked(WDFDEVICE device);
static void FilterStopLocked(WDFDEVICE device);
static NTSTATUS FilterIndicators(DEVICE_CONTEXT* d,USHORT flags);
static void FilterRepeat(DEVICE_CONTEXT* d);
static void FilterReaderReady(DEVICE_CONTEXT* d);
static void FilterApplyIndicators(DEVICE_CONTEXT* d);
// CreateLocked counts and publishes its snapshot under gGate. Every change of
// reader eligibility must use that same gate, including terminal/error paths.
// Never hold it while creating, cancelling or joining the reader thread.
static void FilterSetStop(DEVICE_CONTEXT* d,LONG stopped) {
    WdfWaitLockAcquire(gGate,NULL);
    InterlockedExchange(&d->stop,stopped);
    WdfWaitLockRelease(gGate);
}
_Ret_writes_maybenull_(count)
static PHIDP_VALUE_CAPS FilterAllocateValues(ULONG count) {
    if(!count || count>512)return NULL;
    return ExAllocatePoolZero(NonPagedPoolNx,(SIZE_T)count*sizeof(HIDP_VALUE_CAPS),FILTER_TAG);
}
_Ret_writes_maybenull_(count)
static PHIDP_BUTTON_CAPS FilterAllocateButtons(ULONG count) {
    if(!count || count>512)return NULL;
    return ExAllocatePoolZero(NonPagedPoolNx,(SIZE_T)count*sizeof(HIDP_BUTTON_CAPS),FILTER_TAG);
}
// Keep allocation capacity separate from the HID parser's in/out count.
// Explicit array contracts also preserve the readable extent across HIDP's
// aliased count annotations in older MSVC analysis engines.
static NTSTATUS FilterValueCaps(PHIDP_PREPARSED_DATA data,ULONG capacity,
    _Out_writes_to_(capacity,*count) PHIDP_VALUE_CAPS values,_Out_ PUSHORT count) {
    NTSTATUS status;*count=(USHORT)capacity;
    status=HidP_GetValueCaps(HidP_Input,values,count,data);
    if(status!=HIDP_STATUS_SUCCESS || *count>capacity) { *count=0;return status==HIDP_STATUS_SUCCESS?STATUS_INVALID_BUFFER_SIZE:status; }
    return status;
}
static NTSTATUS FilterButtonCaps(PHIDP_PREPARSED_DATA data,ULONG capacity,
    _Out_writes_to_(capacity,*count) PHIDP_BUTTON_CAPS caps,_Out_ PUSHORT count) {
    NTSTATUS status;*count=(USHORT)capacity;
    status=HidP_GetButtonCaps(HidP_Output,caps,count,data);
    if(status!=HIDP_STATUS_SUCCESS || *count>capacity) { *count=0;return status==HIDP_STATUS_SUCCESS?STATUS_INVALID_BUFFER_SIZE:status; }
    return status;
}
static void FilterReadComplete(WDFREQUEST request,WDFIOTARGET target,PWDF_REQUEST_COMPLETION_PARAMS parameters,WDFCONTEXT context) {
    DEVICE_CONTEXT* d=context;UNREFERENCED_PARAMETER(request);UNREFERENCED_PARAMETER(target);
    
    d->completionStatus=parameters->IoStatus.Status;d->readBytes=parameters->IoStatus.Information;
    KeSetEvent(&d->readDone,IO_NO_INCREMENT,FALSE);
}
static NTSTATUS FilterRead(DEVICE_CONTEXT* d,ULONG_PTR* bytes) {
    NTSTATUS status;WDF_REQUEST_REUSE_PARAMS reuse;ULONG recoveryDelay=1;
    WDFIOTARGET target=WdfDeviceGetIoTarget(d->device);
    if(d->readUsed) {
        WDF_REQUEST_REUSE_PARAMS_INIT(&reuse,WDF_REQUEST_REUSE_NO_FLAGS,STATUS_SUCCESS);
        status=WdfRequestReuse(d->readRequest,&reuse);if(!NT_SUCCESS(status))return status;
    }
    d->readUsed=TRUE;KeClearEvent(&d->readDone);
    status=WdfIoTargetFormatRequestForRead(target,d->readRequest,d->readMemory,NULL,NULL);if(!NT_SUCCESS(status))return status;
    IoGetNextIrpStackLocation(WdfRequestWdmGetIrp(d->readRequest))->FileObject=d->hidFile;
    WdfRequestSetCompletionRoutine(d->readRequest,FilterReadComplete,d);
    if(!WdfRequestSend(d->readRequest,target,NULL))return WdfRequestGetStatus(d->readRequest);
    FilterReaderReady(d);
    for(;;) {
        PVOID events[3]={&d->stopEvent,&d->readDone,&d->maintenanceEvent};
        LARGE_INTEGER timeout={0};PLARGE_INTEGER due=NULL;
        // No polling or periodic cancellation at idle. Only held-key repeat or
        // a full class queue needs a deadline; neither cancels the pending read.
        if(!IcRecovery(d)) {
            timeout.QuadPart=-(LONGLONG)recoveryDelay*10000;due=&timeout;
            recoveryDelay=min(recoveryDelay*2,16u);
        } else recoveryDelay=1;
        if(d->keyboard && d->repeatUsage) {
            ULONGLONG now=KeQueryInterruptTime();
            LONGLONG ticks=d->repeatAt>now?(LONGLONG)(d->repeatAt-now):1;
            if(!due || ticks < -timeout.QuadPart) { timeout.QuadPart=-ticks;due=&timeout; }
        }
        status=KeWaitForMultipleObjects(3,events,WaitAny,Executive,KernelMode,FALSE,due,NULL);
        if(status==STATUS_WAIT_0) {
            // Request storage and hidFile remain alive until completion, even
            // if cancellation races an already completed read.
            WdfRequestCancelSentRequest(d->readRequest);
            KeWaitForSingleObject(&d->readDone,Executive,KernelMode,FALSE,NULL);
            return STATUS_CANCELLED;
        }
        if(status==STATUS_WAIT_1)break;
        if(d->keyboard && !d->stop) {FilterApplyIndicators(d);FilterRepeat(d);}
    }
    *bytes=d->readBytes;return d->completionStatus;
}

static void FilterCloseHid(DEVICE_CONTEXT* d) {
    // Borrowed from the forwarded CREATE. Cleanup stops all transfers before
    // allowing HIDClass to release this file. No circular open/reference on
    // our own PnP stack, so removal does not veto itself.
    d->hidFile=NULL;
}
static NTSTATUS FilterTransfer(DEVICE_CONTEXT* d,BOOLEAN write,PVOID buffer,ULONG length,LONGLONG timeout,ULONG_PTR* bytes) {
    WDFREQUEST request;WDFMEMORY memory;WDF_OBJECT_ATTRIBUTES attributes;WDF_REQUEST_SEND_OPTIONS options;
    WDFIOTARGET target=WdfDeviceGetIoTarget(d->device);NTSTATUS status;
    status=WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES,target,&request);if(!NT_SUCCESS(status))return status;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);attributes.ParentObject=request;
    status=WdfMemoryCreatePreallocated(&attributes,buffer,length,&memory);
    if(NT_SUCCESS(status))status=write?WdfIoTargetFormatRequestForWrite(target,request,memory,NULL,NULL):WdfIoTargetFormatRequestForRead(target,request,memory,NULL,NULL);
    if(NT_SUCCESS(status)) {
        IoGetNextIrpStackLocation(WdfRequestWdmGetIrp(request))->FileObject=d->hidFile;
        WDF_REQUEST_SEND_OPTIONS_INIT(&options,WDF_REQUEST_SEND_OPTION_SYNCHRONOUS|WDF_REQUEST_SEND_OPTION_TIMEOUT);
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options,timeout);
        WdfRequestSend(request,target,&options);status=WdfRequestGetStatus(request);
        if(bytes)*bytes=WdfRequestGetInformation(request);
    }
    WdfObjectDelete(request);return status;
}
void FilterMapperCreate(WDFQUEUE queue,WDFREQUEST request) {
    FilterHidCreate(WdfIoQueueGetDevice(queue),request,WdfRequestGetFileObject(request));
}
void FilterHidCreate(WDFDEVICE device,WDFREQUEST request,WDFFILEOBJECT file) {
    DEVICE_CONTEXT* d=FilterDevice(device);WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;PFILE_OBJECT wdm=WdfFileObjectWdmGetFileObject(file);
    PIO_STACK_LOCATION stack=IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(request));
    ACCESS_MASK access=stack->Parameters.Create.SecurityContext?stack->Parameters.Create.SecurityContext->DesiredAccess:0;
    WdfWaitLockAcquire(d->stateLock,NULL);
    // HIDClass initializes its per-open read context on CREATE. Preserve the
    // class driver's file object, including its access checks, through the stack.
    WdfRequestFormatRequestUsingCurrentType(request);
    WDF_REQUEST_SEND_OPTIONS_INIT(&options,WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);
    WdfRequestSend(request,WdfDeviceGetIoTarget(device),&options);
    status=WdfRequestGetStatus(request);
    
    if(NT_SUCCESS(status) && (access&(FILE_READ_DATA|GENERIC_READ)) && !d->hidFile) {
        d->hidFile=wdm;
        FilterStartLocked(device);
    }
    WdfWaitLockRelease(d->stateLock);
    WdfRequestComplete(request,status);
}
void FilterHidCleanup(WDFFILEOBJECT file) {
    DEVICE_CONTEXT* d=FilterDevice(WdfFileObjectGetDevice(file));
    WdfWaitLockAcquire(d->stateLock,NULL);
    if(d->hidFile==WdfFileObjectWdmGetFileObject(file)) { FilterStopLocked(d->device);FilterCloseHid(d); }
    WdfWaitLockRelease(d->stateLock);
}

static NTSTATUS FilterHidIoctl(WDFDEVICE device, ULONG code, PVOID output, ULONG length) {
    WDF_MEMORY_DESCRIPTOR memory; WDF_REQUEST_SEND_OPTIONS options;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory, output, length);
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    return WdfIoTargetSendIoctlSynchronously(WdfDeviceGetIoTarget(device), NULL, code,
        NULL, &memory, &options, NULL);
}
static void FilterFailure(NTSTATUS status) { UNREFERENCED_PARAMETER(status); }
static void FilterCacheMouseModes(DEVICE_CONTEXT* d) {
    ULONG report;
    for(report=0;report<RTL_NUMBER_OF(d->mouseModes);++report) {
        USHORT x=d->axisCaps[report][0],y=d->axisCaps[report][1];
        d->mouseModes[report]=IcMouseReportMode(x!=0,x?d->values[x-1].IsAbsolute:FALSE,
            y!=0,y?d->values[y-1].IsAbsolute:FALSE);
    }
}
static void FilterFreeReader(DEVICE_CONTEXT* d) {
    if(d->readRequest) {WdfObjectDelete(d->readRequest);d->readRequest=NULL;d->readMemory=NULL;}
    if(d->values) {ExFreePoolWithTag(d->values,FILTER_TAG);d->values=NULL;d->valueCount=0;}
    if(d->keyReports) {ExFreePoolWithTag(d->keyReports,FILTER_TAG);d->keyReports=NULL;}
    if(d->report) {ExFreePoolWithTag(d->report,FILTER_TAG);d->report=NULL;}
    if(d->preparsed) {ExFreePoolWithTag(d->preparsed,FILTER_TAG);d->preparsed=NULL;}
}

static BOOLEAN FilterRetry(DEVICE_CONTEXT* d,ULONG milliseconds) {
    LARGE_INTEGER delay;
    delay.QuadPart=-(LONGLONG)milliseconds*10000;
    return KeWaitForSingleObject(&d->stopEvent,Executive,KernelMode,FALSE,&delay)==STATUS_WAIT_0;
}

static ULONG FilterReadSetting(WDFKEY key,PCWSTR name) {
    UNICODE_STRING value;
    ULONG number=0;
    RtlInitUnicodeString(&value,name);
    if(!NT_SUCCESS(WdfRegistryQueryULong(key,&value,&number)))return 0;
    return number;
}

static void FilterReadMouseSettings(DEVICE_CONTEXT* d) {
    WDFKEY key;
    d->flipWheel=d->flipHorizontal=d->forceAbsolute=FALSE;
    if(!NT_SUCCESS(WdfDeviceOpenRegistryKey(d->device,PLUGPLAY_REGKEY_DEVICE,
        KEY_QUERY_VALUE,WDF_NO_OBJECT_ATTRIBUTES,&key)))return;
    d->flipWheel=FilterReadSetting(key,L"FlipFlopWheel")!=0;
    d->flipHorizontal=FilterReadSetting(key,L"FlipFlopHScroll")!=0;
    d->forceAbsolute=FilterReadSetting(key,L"ForceAbsolute")!=0;
    WdfRegistryClose(key);
}

static NTSTATUS FilterPrepareReader(WDFDEVICE device) {
    DEVICE_CONTEXT* d = FilterDevice(device); HID_COLLECTION_INFORMATION info;
    NTSTATUS status;
    IC_DEVICE_INFO identity={0};
    if(!d->keyboard)FilterReadMouseSettings(d);
    d->absoluteX=d->absoluteY=0;d->prefix=0;d->modifiers.ul=0;
    RtlZeroMemory(d->mouseModes,sizeof(d->mouseModes));
    RtlZeroMemory(d->axisCaps,sizeof(d->axisCaps));
    RtlZeroMemory(d->keyCounts,sizeof(d->keyCounts));
    RtlZeroMemory(&info, sizeof(info));
    status = FilterHidIoctl(device, IOCTL_HID_GET_COLLECTION_INFORMATION, &info, sizeof(info));
    if (!NT_SUCCESS(status)) return IcPrepareStatus(status);
    if (!info.DescriptorSize || info.DescriptorSize > IC_MAPPER_DESCRIPTOR_MAX) return STATUS_INVALID_BUFFER_SIZE;
    identity.vid=info.VendorID;identity.pid=info.ProductID;identity.revision=info.VersionNumber;
    identity.flags=IC_INFO_HID_VALID;identity.instance_token=d->id;
    {
        ULONG bytes=0;
        if(NT_SUCCESS(WdfDeviceQueryProperty(device,DevicePropertyHardwareID,sizeof(identity.hardware_id)-sizeof(USHORT),identity.hardware_id,&bytes)))
            identity.flags|=IC_INFO_HARDWARE_ID_VALID;
        else RtlZeroMemory(identity.hardware_id,sizeof(identity.hardware_id));
        status=WdfDeviceQueryProperty(device,DevicePropertyFriendlyName,sizeof(identity.name)-sizeof(USHORT),identity.name,&bytes);
        if(!NT_SUCCESS(status))status=WdfDeviceQueryProperty(device,DevicePropertyDeviceDescription,sizeof(identity.name)-sizeof(USHORT),identity.name,&bytes);
        if(NT_SUCCESS(status))identity.flags|=IC_INFO_NAME_VALID;
        else RtlZeroMemory(identity.name,sizeof(identity.name));
    }
    d->preparsed = ExAllocatePoolZero(NonPagedPoolNx, info.DescriptorSize, FILTER_TAG);
    if (!d->preparsed) return STATUS_INSUFFICIENT_RESOURCES;
    status = FilterHidIoctl(device, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, d->preparsed, info.DescriptorSize);
    if (!NT_SUCCESS(status)) goto fail;
    status = HidP_GetCaps(d->preparsed, &d->caps);
    if (status != HIDP_STATUS_SUCCESS) goto fail;
    if (IcMapperProfile(info.DescriptorSize,d->caps.UsagePage,d->caps.Usage,
        d->caps.InputReportByteLength,d->caps.NumberInputValueCaps,0)!=IC_MAPPER_PROFILE_OK) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR; goto fail;
    }
    if((d->caps.Usage==6)!=d->keyboard) {status=STATUS_DEVICE_CONFIGURATION_ERROR;goto fail;}
    identity.type=d->keyboard?IC_TYPE_KEYBOARD:IC_TYPE_MOUSE;
    identity.usage_page=d->caps.UsagePage;identity.usage=d->caps.Usage;
    identity.input_bytes=d->caps.InputReportByteLength;identity.output_bytes=d->caps.OutputReportByteLength;identity.feature_bytes=d->caps.FeatureReportByteLength;
    d->valueCount=d->caps.NumberInputValueCaps;
    if(d->valueCount) {
        ULONG i;const ULONG capacity=d->valueCount;USHORT count=(USHORT)capacity;
        if(!capacity || capacity>512) { status=STATUS_INVALID_BUFFER_SIZE;goto fail; }
        d->values=FilterAllocateValues(capacity);
        if(!d->values) { status=STATUS_INSUFFICIENT_RESOURCES;goto fail; }
        status=FilterValueCaps(d->preparsed,capacity,d->values,&count);
        if(status!=HIDP_STATUS_SUCCESS)goto fail;
        if(count>capacity) { status=STATUS_INVALID_BUFFER_SIZE;goto fail; }
        d->valueCount=count;
        if(!d->keyboard)for(i=0;i<count;++i) {
            PHIDP_VALUE_CAPS cap=&d->values[i];
            IcCacheAxes(d->axisCaps[cap->ReportID],(USHORT)i,cap->UsagePage,
                cap->IsRange?cap->Range.UsageMin:cap->NotRange.Usage,
                cap->IsRange?cap->Range.UsageMax:cap->NotRange.Usage);
        }
        if(!d->keyboard)FilterCacheMouseModes(d);
    }
    if(d->keyboard) {
        d->keyReports=ExAllocatePoolZero(NonPagedPoolNx,256*32,FILTER_TAG);
        if(!d->keyReports) { status=STATUS_INSUFFICIENT_RESOURCES;goto fail; }
    }
    d->report = ExAllocatePoolZero(NonPagedPoolNx, d->caps.InputReportByteLength, FILTER_TAG);
    if (!d->report) { status = STATUS_INSUFFICIENT_RESOURCES; goto fail; }
    {
        WDF_OBJECT_ATTRIBUTES attributes;WDF_OBJECT_ATTRIBUTES_INIT(&attributes);attributes.ParentObject=device;
        status=WdfRequestCreate(&attributes,WdfDeviceGetIoTarget(device),&d->readRequest);if(!NT_SUCCESS(status))goto fail;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);attributes.ParentObject=d->readRequest;
        status=WdfMemoryCreatePreallocated(&attributes,d->report,d->caps.InputReportByteLength,&d->readMemory);if(!NT_SUCCESS(status))goto fail;
        d->readUsed=FALSE;
    }
    WdfWaitLockAcquire(gGate,NULL);
    d->info=identity;
    WdfWaitLockRelease(gGate);
    return STATUS_SUCCESS;
fail:
    FilterFreeReader(d);
    FilterFailure(status);
    return IcPrepareStatus(status);
}
static void FilterDeliver(DEVICE_CONTEXT* d,PVOID first,PVOID last) { UNREFERENCED_PARAMETER(last);IcRoute(d,first); }
static BOOLEAN FilterScanCodes(PVOID context, PCHAR codes, ULONG length) {
    DEVICE_CONTEXT* d = context; ULONG i;
    for (i = 0; i < length; ++i) {
        UCHAR code = (UCHAR)codes[i]; KEYBOARD_INPUT_DATA packet = {0};
        if (code == 0xE0 || code == 0xE1) { d->prefix = code == 0xE0 ? KEY_E0 : KEY_E1; continue; }
        packet.MakeCode = code & 0x7F;
        packet.Flags = d->prefix | ((code & 0x80) ? KEY_BREAK : KEY_MAKE); d->prefix = 0;
        FilterDeliver(d, &packet, &packet + 1);
    }
    return TRUE;
}
static void FilterApplyKeys(DEVICE_CONTEXT* d) {
    USAGE changed[256],repeat=0;ULONG n,i;const UCHAR* mapped=d->keys;NTSTATUS status;
    // Only this reader writes keys; delivery callbacks cannot change the union.
    n=0;for(i=4;i<256;++i)if(d->mapped[i]&&!mapped[i])changed[n++]=(USAGE)i;
    if(n) {
        status=HidP_TranslateUsagesToI8042ScanCodes(changed,n,HidP_Keyboard_Break,&d->modifiers,FilterScanCodes,d);
        if(status!=HIDP_STATUS_SUCCESS)FilterFailure(status);
    }
    if(d->repeatUsage && !mapped[d->repeatUsage])d->repeatUsage=0;
    n=0;for(i=4;i<256;++i)if(!d->mapped[i]&&mapped[i]) {
        changed[n++]=(USAGE)i;
        if(i<0xE0)repeat=(USAGE)i;
    }
    if(repeat) {
        USHORT delay;WdfSpinLockAcquire(gLock);delay=d->typematic.Delay;WdfSpinLockRelease(gLock);
        d->repeatUsage=repeat;d->repeatAt=KeQueryInterruptTime()+(ULONGLONG)delay*10000;
    }
    if(n) {
        status=HidP_TranslateUsagesToI8042ScanCodes(changed,n,HidP_Keyboard_Make,&d->modifiers,FilterScanCodes,d);
        if(status!=HIDP_STATUS_SUCCESS)FilterFailure(status);
    }
    RtlCopyMemory(d->mapped,mapped,sizeof(d->mapped));
}
static void FilterRepeat(DEVICE_CONTEXT* d) {
    if(d->repeatUsage && KeQueryInterruptTime()>=d->repeatAt) {
        USHORT rate;NTSTATUS status;
        WdfSpinLockAcquire(gLock);rate=d->typematic.Rate;WdfSpinLockRelease(gLock);
        status=HidP_TranslateUsagesToI8042ScanCodes(&d->repeatUsage,1,HidP_Keyboard_Make,&d->modifiers,FilterScanCodes,d);
        if(status!=HIDP_STATUS_SUCCESS)FilterFailure(status);
        d->repeatAt=KeQueryInterruptTime()+10000000/max(1,rate);
    }
}
static void FilterKeyboard(DEVICE_CONTEXT* d) {
    USAGE usages[256]; ULONG count = RTL_NUMBER_OF(usages), i;
    UCHAR reportKeys[32]={0},current[256];PUCHAR previous=d->keyReports+d->report[0]*32;NTSTATUS status;
    status = HidP_GetUsages(HidP_Input, 7, 0, usages, &count, d->preparsed,
        (PCHAR)d->report, d->caps.InputReportByteLength);
    if(status==HIDP_STATUS_INCOMPATIBLE_REPORT_ID || status==HIDP_STATUS_USAGE_NOT_FOUND) return;
    if (status != HIDP_STATUS_SUCCESS) { FilterFailure(status); return; }
    for (i = 0; i < count; ++i) {
        if (usages[i] >= 1 && usages[i] <= 3) return; // Rollover is not a release-all report.
        if (usages[i] >= 4 && usages[i] < 256) reportKeys[usages[i]/8]|=(UCHAR)(1u<<(usages[i]%8));
    }
    // Most repeats are identical. Compare one compact report before copying or
    // merging the global state; only changed usage bits need owner updates.
    if(RtlCompareMemory(previous,reportKeys,sizeof(reportKeys))==sizeof(reportKeys))return;
    RtlCopyMemory(current,d->keys,sizeof(current));
    if(!IcHidMergeKeyReport(previous,reportKeys,d->keyCounts,current))return;
    IcPhysicalKeys(d,current);
    RtlCopyMemory(d->keys,current,sizeof(current));FilterApplyKeys(d);
}
static LONG FilterAxis(DEVICE_CONTEXT* d, USAGE page, USAGE usage, BOOLEAN* valid) {
    ULONG axis=page==0x0C?3:usage==0x30?0:usage==0x31?1:2;
    USHORT index=d->axisCaps[d->report[0]][axis];
    if(index) {
        PHIDP_VALUE_CAPS cap=&d->values[index-1];ULONG raw=0;LONG result;NTSTATUS status;
        status=HidP_GetUsageValue(HidP_Input,page,cap->LinkCollection,usage,&raw,d->preparsed,(PCHAR)d->report,d->caps.InputReportByteLength);
        if(status!=HIDP_STATUS_SUCCESS) { FilterFailure(status);return 0; }
        if(cap->LogicalMin<0 && cap->BitSize && cap->BitSize<32 && (raw&(1u<<(cap->BitSize-1))))raw|=(~0u<<cap->BitSize);
        result=(LONG)raw;
        if((cap->IsAbsolute || d->forceAbsolute) && page==1 && (usage==0x30 || usage==0x31)) {
            LONGLONG range=(LONGLONG)cap->LogicalMax-cap->LogicalMin;
            if(range<=0 || (cap->HasNull && (result<cap->LogicalMin || result>cap->LogicalMax)))return 0;
            result=(LONG)max(0,min(65535,((LONGLONG)result-cap->LogicalMin)*65535/range));
            if(usage==0x30)d->absoluteX=result;else d->absoluteY=result;
        }
        if(valid)*valid=TRUE;
        return result;
    }
    return 0;
}
static void FilterMouse(DEVICE_CONTEXT* d) {
    USAGE usages[32]; ULONG count = RTL_NUMBER_OF(usages), i; UCHAR buttons = 0, mapped;
    MOUSE_INPUT_DATA packet = {0}; LONG x, y, wheel, horizontal; NTSTATUS status;
    const UCHAR mode=d->mouseModes[d->report[0]];
    BOOLEAN validX=FALSE,validY=FALSE;
    const BOOLEAN absolute=(mode&IC_MOUSE_REPORT_ABSOLUTE)!=0 ||
        (d->forceAbsolute && (mode&(IC_MOUSE_REPORT_X|IC_MOUSE_REPORT_Y))!=0);
    status = HidP_GetUsages(HidP_Input, 9, 0, usages, &count, d->preparsed,
        (PCHAR)d->report, d->caps.InputReportByteLength);
    if(status==HIDP_STATUS_SUCCESS) {
        for (i = 0; i < count; ++i) if (usages[i] >= 1 && usages[i] <= 8) buttons |= (UCHAR)(1u << (usages[i] - 1));
        {
            UCHAR previous=d->buttonReports[d->report[0]];
            for(i=0;i<8;++i)if((previous^buttons)&(1u<<i)) {
                if(buttons&(1u<<i))++d->buttonOwners[i];else --d->buttonOwners[i];
                if(d->buttonOwners[i])d->physicalButtons|=(UCHAR)(1u<<i);
                else d->physicalButtons&=(UCHAR)~(1u<<i);
            }
            d->buttonReports[d->report[0]]=buttons;
        }
    } else if(status!=HIDP_STATUS_USAGE_NOT_FOUND && status!=HIDP_STATUS_INCOMPATIBLE_REPORT_ID) { FilterFailure(status);return; }
    buttons=d->physicalButtons;
    x=FilterAxis(d,1,0x30,&validX);
    y=FilterAxis(d,1,0x31,&validY);
    wheel=FilterAxis(d,1,0x38,NULL);
    horizontal=FilterAxis(d,0x0C,0x238,NULL);
    mapped=buttons;
    for (i = 0; i < 5; ++i) if ((mapped ^ d->buttons) & (1u << i))
        packet.ButtonFlags |= (USHORT)(1u << (2 * i + ((mapped & (1u << i)) ? 0 : 1)));
    if(absolute) {
        // A null or absent axis must never turn a button-only report into a
        // move to the origin. Reuse its last position only on real movement.
        if(validX || validY) {
            packet.Flags=MOUSE_MOVE_ABSOLUTE|MOUSE_VIRTUAL_DESKTOP;
            packet.LastX=validX?max(0,min(65535,x)):d->absoluteX;
            packet.LastY=validY?max(0,min(65535,y)):d->absoluteY;
        }
    } else {
        packet.LastX=x;
        packet.LastY=y;
    }
    if(wheel) {
        packet.ButtonFlags|=MOUSE_WHEEL;
        packet.ButtonData=(USHORT)FilterScaleWheel(wheel,d->flipWheel);
    }
    if(packet.ButtonFlags || packet.LastX || packet.LastY || packet.Flags)FilterDeliver(d,&packet,&packet+1);
    if(horizontal) {
        RtlZeroMemory(&packet,sizeof(packet));
        packet.ButtonFlags=MOUSE_HWHEEL;
        packet.ButtonData=(USHORT)FilterScaleWheel(horizontal,d->flipHorizontal);
        FilterDeliver(d,&packet,&packet+1);
    }
    d->buttons = mapped;
}

static void FilterReaderReady(DEVICE_CONTEXT* d) {
    WdfWaitLockAcquire(gGate,NULL);
    if(!d->readerReady && !d->stop) {
        d->readerReady=TRUE;
        IcDeviceReadyLocked(d);
    }
    WdfWaitLockRelease(gGate);
}

static void FilterReaderOffline(DEVICE_CONTEXT* d) {
    BOOLEAN wasReady;
    WdfWaitLockAcquire(gGate,NULL);
    wasReady=d->readerReady;
    d->readerReady=FALSE;
    WdfWaitLockRelease(gGate);
    if(wasReady)IcDeviceStopping(d);
    RtlZeroMemory(d->keys,sizeof(d->keys));RtlZeroMemory(d->mapped,sizeof(d->mapped));
    RtlZeroMemory(d->keyCounts,sizeof(d->keyCounts));RtlZeroMemory(d->buttonReports,sizeof(d->buttonReports));
    RtlZeroMemory(d->buttonOwners,sizeof(d->buttonOwners));d->physicalButtons=0;
    if(d->keyReports)RtlZeroMemory(d->keyReports,256*32);
    d->buttons=0;d->repeatUsage=0;d->modifiers.ul=0;d->prefix=0;
}

static void FilterReadThread(PVOID context) {
    DEVICE_CONTEXT* d=context;
    ULONG retryMs=25,failures=0;
    KeWaitForSingleObject(&d->readerStarted,Executive,KernelMode,FALSE,NULL);
    while(!InterlockedCompareExchange(&d->stop,0,0)) {
        NTSTATUS status;
        ULONG_PTR received=0;
        if(!d->readRequest) {
            status=FilterPrepareReader(d->device);
            InterlockedExchange(&d->readStatus,status);
            if(!NT_SUCCESS(status)) {
                if(FilterRetry(d,retryMs))break;
                retryMs=min(retryMs*2,2000u);
                continue;
            }
            (void)FilterReserveInputBuffers(d->device,d->hidFile);
            if(d->keyboard) {
                InterlockedExchange(&d->indicatorDirty,TRUE);
                FilterApplyIndicators(d);
            }
        }
        if(d->stop)break;
        if(d->keyboard)FilterApplyIndicators(d);
        status=FilterRead(d,&received);
        if(d->stop)break;
        if(NT_SUCCESS(status) && received!=d->caps.InputReportByteLength)status=STATUS_INVALID_BUFFER_SIZE;
        InterlockedExchange(&d->readStatus,status);
        if(!NT_SUCCESS(status)) {
            // A transport may return disconnection while Windows is still
            // powering the devnode. Honor stop, back off, then rebuild the
            // request and parser if the same generation stays unhealthy.
            if(++failures==4) {
                FilterReaderOffline(d);
                FilterFreeReader(d);
                failures=0;
            }
            if(FilterRetry(d,retryMs))break;
            retryMs=min(retryMs*2,2000u);
            continue;
        }
        retryMs=25;
        failures=0;
        if(d->keyboard) {FilterRepeat(d);FilterKeyboard(d);}
        else FilterMouse(d);
    }
    FilterReaderOffline(d);
    FilterFreeReader(d);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static void FilterStartLocked(WDFDEVICE device) {
    DEVICE_CONTEXT* d=FilterDevice(device);
    HANDLE thread;
    PETHREAD object=NULL;
    NTSTATUS status;
    OBJECT_ATTRIBUTES attributes;
    // CREATE, CONNECT and the working-state transition can arrive in any order.
    // Cleanup stops and joins this worker before releasing the borrowed HID file.
    if(!d->powered || !d->hidFile || !d->connection.ClassService || d->thread)return;
    KeClearEvent(&d->stopEvent);KeClearEvent(&d->maintenanceEvent);KeClearEvent(&d->readerStarted);
    FilterSetStop(d,0);
    InitializeObjectAttributes(&attributes,NULL,OBJ_KERNEL_HANDLE,NULL,NULL);
    status=PsCreateSystemThread(&thread,THREAD_ALL_ACCESS,&attributes,NULL,NULL,FilterReadThread,d);
    if(NT_SUCCESS(status)) {
        status=ObReferenceObjectByHandle(thread,SYNCHRONIZE,*PsThreadType,KernelMode,(PVOID*)&object,NULL);
        if(NT_SUCCESS(status)) {
            WdfWaitLockAcquire(gGate,NULL);d->thread=object;WdfWaitLockRelease(gGate);
        } else {
            FilterSetStop(d,1);
            KeSetEvent(&d->stopEvent,IO_NO_INCREMENT,FALSE);
        }
        KeSetEvent(&d->readerStarted,IO_NO_INCREMENT,FALSE);
        if(!NT_SUCCESS(status))ZwWaitForSingleObject(thread,FALSE,NULL);
        ZwClose(thread);
    }
    if(!NT_SUCCESS(status)) {
        FilterSetStop(d,1);
        InterlockedExchange(&d->readStatus,status);
        WdfTimerStart(d->retryTimer,WDF_REL_TIMEOUT_IN_MS(500));
    }
}

static void FilterStopLocked(WDFDEVICE device) {
    DEVICE_CONTEXT* d=FilterDevice(device);
    FilterSetStop(d,1);
    WdfTimerStop(d->retryTimer,FALSE);
    if(d->thread) {
        KeSetEvent(&d->stopEvent,IO_NO_INCREMENT,FALSE);
        KeWaitForSingleObject(d->thread,Executive,KernelMode,FALSE,NULL);
        WdfWaitLockAcquire(gGate,NULL);
        ObDereferenceObject(d->thread);d->thread=NULL;
        WdfWaitLockRelease(gGate);
    }
}

static EVT_WDF_TIMER FilterRetryStart;
static VOID FilterRetryStart(WDFTIMER timer) {
    WDFDEVICE device=(WDFDEVICE)WdfTimerGetParentObject(timer);
    DEVICE_CONTEXT* d=FilterDevice(device);
    WdfWaitLockAcquire(d->stateLock,NULL);
    FilterStartLocked(device);
    WdfWaitLockRelease(d->stateLock);
}

NTSTATUS FilterInitialize(WDFDEVICE device,BOOLEAN keyboard) {
    DEVICE_CONTEXT* d=FilterDevice(device);
    WDF_TIMER_CONFIG timer;
    WDF_OBJECT_ATTRIBUTES attributes;
    d->keyboard=keyboard;
    d->info.type=keyboard?IC_TYPE_KEYBOARD:IC_TYPE_MOUSE;
    d->info.instance_token=d->id;
    d->typematic.Rate=30;
    d->typematic.Delay=500;
    d->stop=1;
    d->readStatus=STATUS_DEVICE_NOT_READY;
    KeInitializeEvent(&d->readDone,SynchronizationEvent,FALSE);
    KeInitializeEvent(&d->readerStarted,NotificationEvent,FALSE);
    KeInitializeEvent(&d->stopEvent,NotificationEvent,FALSE);
    KeInitializeEvent(&d->maintenanceEvent,SynchronizationEvent,FALSE);
    WDF_TIMER_CONFIG_INIT(&timer,FilterRetryStart);
    timer.AutomaticSerialization=FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject=device;
    attributes.ExecutionLevel=WdfExecutionLevelPassive;
    return WdfTimerCreate(&timer,&attributes,&d->retryTimer);
}

NTSTATUS FilterPrepare(WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(raw);
    UNREFERENCED_PARAMETER(translated);
    return IcRegister(FilterDevice(device));
}

NTSTATUS FilterStart(WDFDEVICE device) {
    DEVICE_CONTEXT* d=FilterDevice(device);
    WdfWaitLockAcquire(d->stateLock,NULL);
    d->powered=TRUE;
    FilterStartLocked(device);
    WdfWaitLockRelease(d->stateLock);
    return STATUS_SUCCESS;
}

void FilterStop(WDFDEVICE device) {
    DEVICE_CONTEXT* d=FilterDevice(device);
    WdfWaitLockAcquire(d->stateLock,NULL);
    d->powered=FALSE;
    FilterStopLocked(device);
    WdfWaitLockRelease(d->stateLock);
}

NTSTATUS FilterSuspend(WDFDEVICE device) {FilterStop(device);return STATUS_SUCCESS;}
NTSTATUS FilterRelease(WDFDEVICE device,WDFCMRESLIST resources) {
    UNREFERENCED_PARAMETER(resources);
    DEVICE_CONTEXT* d=FilterDevice(device);
    WdfWaitLockAcquire(d->stateLock,NULL);
    d->powered=FALSE;
    FilterStopLocked(device);
    FilterCloseHid(d);
    IcUnregister(d);
    WdfWaitLockRelease(d->stateLock);
    return STATUS_SUCCESS;
}

static NTSTATUS FilterIndicators(DEVICE_CONTEXT* d,USHORT flags) {
    PHIDP_BUTTON_CAPS caps;PUCHAR report;USHORT count=d->caps.NumberOutputButtonCaps;const ULONG capacity=count;ULONG i;
    UCHAR done[256]={0};NTSTATUS status=STATUS_SUCCESS;
    if(!d->hidFile || !d->preparsed || !count || !d->caps.OutputReportByteLength)return STATUS_SUCCESS;
    if(!capacity || capacity>512 || d->caps.OutputReportByteLength>4096)return STATUS_NOT_SUPPORTED;
    caps=FilterAllocateButtons(capacity);
    report=ExAllocatePoolZero(NonPagedPoolNx,d->caps.OutputReportByteLength,FILTER_TAG);
    if(!caps || !report) { status=STATUS_INSUFFICIENT_RESOURCES;goto end; }
    status=FilterButtonCaps(d->preparsed,capacity,caps,&count);if(status!=HIDP_STATUS_SUCCESS)goto end;
    if(count>capacity) { status=STATUS_INVALID_BUFFER_SIZE;goto end; }
    for(i=0;i<capacity && i<count;++i)if(caps[i].UsagePage==8 && !done[caps[i].ReportID]) {
        USHORT j;
        done[caps[i].ReportID]=1;
        status=HidP_InitializeReportForID(HidP_Output,caps[i].ReportID,d->preparsed,(PCHAR)report,d->caps.OutputReportByteLength);
        if(status!=HIDP_STATUS_SUCCESS)goto end;
        for(j=0;j<capacity && j<count;++j)if(caps[j].UsagePage==8 && caps[j].ReportID==caps[i].ReportID) {
            USAGE usages[3];ULONG n=0,k;
            for(k=1;k<=3;++k) {
                USHORT bit=k==1?KEYBOARD_NUM_LOCK_ON:k==2?KEYBOARD_CAPS_LOCK_ON:KEYBOARD_SCROLL_LOCK_ON;
                if((flags&bit) && (caps[j].IsRange ? k>=caps[j].Range.UsageMin&&k<=caps[j].Range.UsageMax : k==caps[j].NotRange.Usage))usages[n++]=(USAGE)k;
            }
            if(n) { status=HidP_SetUsages(HidP_Output,8,caps[j].LinkCollection,usages,&n,d->preparsed,(PCHAR)report,d->caps.OutputReportByteLength);if(status!=HIDP_STATUS_SUCCESS)goto end; }
        }
        status=FilterTransfer(d,TRUE,report,d->caps.OutputReportByteLength,WDF_REL_TIMEOUT_IN_SEC(1),NULL);
        if(!NT_SUCCESS(status))goto end;
    }
    status=STATUS_SUCCESS;
end:
    if(caps)ExFreePoolWithTag(caps,FILTER_TAG);if(report)ExFreePoolWithTag(report,FILTER_TAG);
    return status;
}
static void FilterApplyIndicators(DEVICE_CONTEXT* d) {
    if(InterlockedExchange(&d->indicatorDirty,FALSE))
        (void)FilterIndicators(d,(USHORT)InterlockedCompareExchange(&d->indicators,0,0));
}

void FilterInternal(WDFQUEUE queue, WDFREQUEST request, size_t outSize, size_t inSize, ULONG code) {
    DEVICE_CONTEXT* d = FilterDevice(WdfIoQueueGetDevice(queue)); PVOID buffer;
    NTSTATUS status = STATUS_NOT_SUPPORTED; size_t bytes = 0;
    UNREFERENCED_PARAMETER(outSize); UNREFERENCED_PARAMETER(inSize);
    if((code==IOCTL_INTERNAL_KEYBOARD_CONNECT || code==IOCTL_INTERNAL_MOUSE_CONNECT ||
        code==IOCTL_INTERNAL_KEYBOARD_DISCONNECT || code==IOCTL_INTERNAL_MOUSE_DISCONNECT) && WdfRequestGetRequestorMode(request)!=KernelMode) {
        WdfRequestComplete(request,STATUS_ACCESS_DENIED);return;
    }
    WdfWaitLockAcquire(d->stateLock,NULL);
    if (code == IOCTL_INTERNAL_KEYBOARD_CONNECT || code == IOCTL_INTERNAL_MOUSE_CONNECT) {
        status=WdfRequestRetrieveInputBuffer(request,sizeof(CONNECT_DATA),&buffer,NULL);
        if(NT_SUCCESS(status)) {
            const CONNECT_DATA* incoming=buffer;
            if(!incoming->ClassService || !incoming->ClassDeviceObject)status=STATUS_INVALID_PARAMETER;
            else if(d->connection.ClassService &&
                (d->connection.ClassService!=incoming->ClassService ||
                 d->connection.ClassDeviceObject!=incoming->ClassDeviceObject))status=STATUS_SHARING_VIOLATION;
            else {
                WdfWaitLockAcquire(gGate,NULL);d->connection=*incoming;WdfWaitLockRelease(gGate);
                FilterStartLocked(d->device);
            }
        }
    } else if (code == IOCTL_INTERNAL_KEYBOARD_DISCONNECT || code == IOCTL_INTERNAL_MOUSE_DISCONNECT) {
        FilterStopLocked(d->device);WdfWaitLockAcquire(gGate,NULL);RtlZeroMemory(&d->connection,sizeof(d->connection));WdfWaitLockRelease(gGate);status=STATUS_SUCCESS;
    } else if (code == IOCTL_KEYBOARD_QUERY_ATTRIBUTES) {
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(KEYBOARD_ATTRIBUTES), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            KEYBOARD_ATTRIBUTES* a = buffer; RtlZeroMemory(a, sizeof(*a));
            a->KeyboardIdentifier.Type = 4; a->NumberOfFunctionKeys = 12; a->NumberOfIndicators = 3;
            a->NumberOfKeysTotal = 101; a->InputDataQueueLength = 100 * sizeof(KEYBOARD_INPUT_DATA); bytes = sizeof(*a);
            a->KeyRepeatMinimum.Rate=2;a->KeyRepeatMinimum.Delay=250;a->KeyRepeatMaximum.Rate=30;a->KeyRepeatMaximum.Delay=1000;
        }
    } else if(code==IOCTL_KEYBOARD_QUERY_TYPEMATIC && d->keyboard) {
        status=WdfRequestRetrieveOutputBuffer(request,sizeof(d->typematic),&buffer,NULL);
        if(NT_SUCCESS(status)) { WdfSpinLockAcquire(gLock);*(PKEYBOARD_TYPEMATIC_PARAMETERS)buffer=d->typematic;WdfSpinLockRelease(gLock);bytes=sizeof(d->typematic); }
    } else if(code==IOCTL_KEYBOARD_SET_TYPEMATIC && d->keyboard) {
        status=WdfRequestRetrieveInputBuffer(request,sizeof(d->typematic),&buffer,NULL);
        if(NT_SUCCESS(status)) {
            PKEYBOARD_TYPEMATIC_PARAMETERS p=buffer;
            if(p->Rate<2 || p->Rate>30 || p->Delay<250 || p->Delay>1000)status=STATUS_INVALID_PARAMETER;
            else { WdfSpinLockAcquire(gLock);d->typematic=*p;WdfSpinLockRelease(gLock); }
        }
    } else if(code==IOCTL_KEYBOARD_QUERY_INDICATORS && d->keyboard) {
        status=WdfRequestRetrieveOutputBuffer(request,sizeof(KEYBOARD_INDICATOR_PARAMETERS),&buffer,NULL);
        if(NT_SUCCESS(status)) { PKEYBOARD_INDICATOR_PARAMETERS p=buffer;p->UnitId=0;p->LedFlags=(USHORT)InterlockedCompareExchange(&d->indicators,0,0);bytes=sizeof(*p); }
    } else if(code==IOCTL_KEYBOARD_SET_INDICATORS && d->keyboard) {
        status=WdfRequestRetrieveInputBuffer(request,sizeof(KEYBOARD_INDICATOR_PARAMETERS),&buffer,NULL);
        if(NT_SUCCESS(status)) {
            InterlockedExchange(&d->indicators,((PKEYBOARD_INDICATOR_PARAMETERS)buffer)->LedFlags);
            InterlockedExchange(&d->indicatorDirty,TRUE);
            KeSetEvent(&d->maintenanceEvent,IO_NO_INCREMENT,FALSE);
        }
    } else if(code==IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION && d->keyboard) {
        const ULONG length=FIELD_OFFSET(KEYBOARD_INDICATOR_TRANSLATION,IndicatorList)+3*sizeof(INDICATOR_LIST);
        status=WdfRequestRetrieveOutputBuffer(request,length,&buffer,NULL);
        if(NT_SUCCESS(status)) {
            PKEYBOARD_INDICATOR_TRANSLATION p=buffer;RtlZeroMemory(p,length);p->NumberOfIndicatorKeys=3;
            p->IndicatorList[0].MakeCode=0x3A;p->IndicatorList[0].IndicatorFlags=KEYBOARD_CAPS_LOCK_ON;
            p->IndicatorList[1].MakeCode=0x45;p->IndicatorList[1].IndicatorFlags=KEYBOARD_NUM_LOCK_ON;
            p->IndicatorList[2].MakeCode=0x46;p->IndicatorList[2].IndicatorFlags=KEYBOARD_SCROLL_LOCK_ON;bytes=length;
        }
    } else if (code == IOCTL_MOUSE_QUERY_ATTRIBUTES) {
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(MOUSE_ATTRIBUTES), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            MOUSE_ATTRIBUTES* a = buffer; RtlZeroMemory(a, sizeof(*a));
            a->MouseIdentifier = MOUSE_HID_HARDWARE; a->NumberOfButtons = 5;
            a->SampleRate = 100; a->InputDataQueueLength = 100 * sizeof(MOUSE_INPUT_DATA); bytes = sizeof(*a);
        }
    }
    WdfWaitLockRelease(d->stateLock);WdfRequestCompleteWithInformation(request, status, bytes);
}
