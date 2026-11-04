// Private implementation of the published oblitum Interception device ABI.
// All slot/client/queue state and source lifetimes are protected by gGate.
#include <ntstrsafe.h>
#define IV2_LEGACY_SLOTS 20u
#define IV2_LEGACY_CLIENTS 16u
#define IV2_LEGACY_CAPACITY 1024u
#define IV2_IOCTL(fn) CTL_CODE(FILE_DEVICE_UNKNOWN,fn,METHOD_BUFFERED,FILE_ANY_ACCESS)
typedef union LEGACY_PACKET {KEYBOARD_INPUT_DATA key;MOUSE_INPUT_DATA mouse;} LEGACY_PACKET;
typedef struct LEGACY_FILE {
    LIST_ENTRY link;ULONG slot,head,count;LONG precedence;ULONGLONG order;
    PKEVENT event;LEGACY_PACKET* packets;USHORT filter;
    BOOLEAN linked,counted,touched;NTSTATUS revoked;
} LEGACY_FILE;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LEGACY_FILE,LegacyFile)
typedef struct LEGACY_ENDPOINT {ULONG slot;} LEGACY_ENDPOINT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LEGACY_ENDPOINT,LegacyEndpoint)
typedef struct LEGACY_SLOT {
    WDFDEVICE endpoint;DEVICE_CONTEXT* target;LIST_ENTRY clients;
    USHORT instance[256];ULONG count;
} LEGACY_SLOT;
static LEGACY_SLOT legacySlots[IV2_LEGACY_SLOTS];
static ULONG legacyOpenFiles;
static ULONGLONG legacyOrder;
static void LegacyStateInitialize(void) {
    ULONG i;for(i=0;i<IV2_LEGACY_SLOTS;++i)InitializeListHead(&legacySlots[i].clients);
}
EVT_WDF_DEVICE_FILE_CREATE LegacyCreate;
EVT_WDF_FILE_CLEANUP LegacyCleanup;
EVT_WDF_FILE_CLOSE LegacyClose;
EVT_WDF_IO_IN_CALLER_CONTEXT LegacyCaller;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LegacyIoctl;

static BOOLEAN LegacySameInstance(const USHORT* a,const USHORT* b) {
    ULONG i;for(i=0;i<256;++i){if(a[i]!=b[i])return FALSE;if(!a[i])return TRUE;}return FALSE;
}
static void LegacyBindLocked(DEVICE_CONTEXT* d) {
    ULONG i,start=d->keyboard?0:10,empty=IV2_LEGACY_SLOTS;
    if(d->stop || !d->thread || !d->connection.ClassService)return;
    for(i=start;i<start+10;++i) {
        LEGACY_SLOT* slot=&legacySlots[i];
        if(slot->target==d)return;
        if(slot->instance[0] && LegacySameInstance(slot->instance,d->instance)){slot->target=d;return;}
        if(!slot->instance[0] && empty==IV2_LEGACY_SLOTS)empty=i;
    }
    if(empty<IV2_LEGACY_SLOTS) {
        legacySlots[empty].target=d;
        RtlCopyMemory(legacySlots[empty].instance,d->instance,sizeof(d->instance));
    }
}
static void LegacyDetachLocked(DEVICE_CONTEXT* d) {
    ULONG i;for(i=0;i<IV2_LEGACY_SLOTS;++i)if(legacySlots[i].target==d) {
        PLIST_ENTRY p;LEGACY_SLOT* slot=&legacySlots[i];slot->target=NULL;
        for(p=slot->clients.Flink;p!=&slot->clients;p=p->Flink) {
            LEGACY_FILE* f=CONTAINING_RECORD(p,LEGACY_FILE,link);
            f->head=f->count=0;if(f->event)KeClearEvent(f->event);
        }
    }
}
static BOOLEAN LegacyBusyLocked(void) {
    ULONG i;for(i=0;i<IV2_LEGACY_SLOTS;++i) {
        PLIST_ENTRY p;for(p=legacySlots[i].clients.Flink;p!=&legacySlots[i].clients;p=p->Flink)
            if(CONTAINING_RECORD(p,LEGACY_FILE,link)->filter)return TRUE;
    }return FALSE;
}
static void LegacyRevokeLocked(ULONG reason) {
    ULONG i;for(i=0;i<IV2_LEGACY_SLOTS;++i) {
        LEGACY_SLOT* slot=&legacySlots[i];PLIST_ENTRY p;BOOLEAN used=FALSE;
        for(p=slot->clients.Flink;p!=&slot->clients;p=p->Flink) {
            LEGACY_FILE* f=CONTAINING_RECORD(p,LEGACY_FILE,link);
            used|=(BOOLEAN)(f->filter || f->touched || f->count);
            f->filter=0;f->head=f->count=0;
            f->revoked=reason==IC_RESET_TRIAL_EXPIRED?STATUS_EVALUATION_EXPIRATION:
                reason==IC_RESET_OVERFLOW?STATUS_BUFFER_OVERFLOW:STATUS_CANCELLED;
            if(f->event)KeSetEvent(f->event,IO_NO_INCREMENT,FALSE);
        }
        if(used && slot->target){InterlockedExchange(&slot->target->recover,TRUE);KeSetEvent(&slot->target->maintenanceEvent,IO_NO_INCREMENT,FALSE);}
    }
}
static BOOLEAN LegacyMatches(BOOLEAN keyboard,USHORT filter,const LEGACY_PACKET* packet) {
    if(!filter)return FALSE;if(filter==0xffff)return TRUE;
    if(keyboard){ULONG flags=packet->key.Flags;return (BOOLEAN)((filter&((flags&KEY_BREAK)?2u:1u)) || (filter&((flags&~1u)<<1)));}
    return (BOOLEAN)((filter&packet->mouse.ButtonFlags) || ((filter&0x1000) &&
        (packet->mouse.LastX || packet->mouse.LastY || (packet->mouse.Flags&MOUSE_MOVE_ABSOLUTE))));
}
static BOOLEAN LegacyAfter(const LEGACY_FILE* next,const LEGACY_FILE* from) {
    return !from || next->precedence<from->precedence ||
        (next->precedence==from->precedence && next->order>from->order);
}
static LONG LegacyDispatchLocked(ULONG id,LEGACY_FILE* from,const LEGACY_PACKET* packet) {
    LEGACY_SLOT* slot=&legacySlots[id];LEGACY_FILE* winner=NULL;PLIST_ENTRY p;
    for(p=slot->clients.Flink;p!=&slot->clients;p=p->Flink) {
        LEGACY_FILE* f=CONTAINING_RECORD(p,LEGACY_FILE,link);
        if(f==from || !NT_SUCCESS(f->revoked) || !f->event || !f->packets || !LegacyAfter(f,from) || !LegacyMatches(id<10,f->filter,packet))continue;
        if(!winner || f->precedence>winner->precedence || (f->precedence==winner->precedence && f->order<winner->order))winner=f;
    }
    if(!winner)return 0;
    if(winner->count==IV2_LEGACY_CAPACITY){LegacyRevokeLocked(IC_RESET_OVERFLOW);return -1;}
    winner->packets[(winner->head+winner->count)%IV2_LEGACY_CAPACITY]=*packet;
    ++winner->count;KeSetEvent(winner->event,IO_NO_INCREMENT,FALSE);return 1;
}
static LONG LegacyRouteLocked(DEVICE_CONTEXT* d,PVOID raw) {
    ULONG i;LEGACY_PACKET packet;IcTrialCheckLocked();
    RtlZeroMemory(&packet,sizeof(packet));RtlCopyMemory(&packet,raw,d->keyboard?sizeof(packet.key):sizeof(packet.mouse));
    for(i=d->keyboard?0u:10u;i<(d->keyboard?10u:20u);++i)if(legacySlots[i].target==d)return LegacyDispatchLocked(i,NULL,&packet);
    return 0;
}
void LegacyCreate(WDFDEVICE device,WDFREQUEST request,WDFFILEOBJECT file) {
    NTSTATUS status;LEGACY_FILE* f=LegacyFile(file);ULONG index=LegacyEndpoint(device)->slot;
    WdfWaitLockAcquire(gLifetime,NULL);WdfWaitLockAcquire(gGate,NULL);
    IcTrialCheckLocked();status=IcTrialAccessLocked();
    if(NT_SUCCESS(status) && legacySlots[index].count>=IV2_LEGACY_CLIENTS)status=STATUS_TOO_MANY_OPENED_FILES;
    if(NT_SUCCESS(status)) {
        if(!legacyOpenFiles) {
            ULONG i;PLIST_ENTRY p;
            for(i=0;i<IV2_LEGACY_SLOTS;++i){legacySlots[i].target=NULL;RtlZeroMemory(legacySlots[i].instance,sizeof(legacySlots[i].instance));}
            for(p=gDevices.Flink;p!=&gDevices;p=p->Flink)LegacyBindLocked(CONTAINING_RECORD(p,DEVICE_CONTEXT,link));
        }
        f->slot=index;f->order=++legacyOrder;f->revoked=STATUS_SUCCESS;
        InsertTailList(&legacySlots[index].clients,&f->link);++legacySlots[index].count;
        f->linked=f->counted=TRUE;++legacyOpenFiles;++gOpenFiles;
    }
    WdfWaitLockRelease(gGate);WdfWaitLockRelease(gLifetime);WdfRequestComplete(request,status);
}
void LegacyCleanup(WDFFILEOBJECT file) {
    LEGACY_FILE* f=LegacyFile(file);WdfWaitLockAcquire(gGate,NULL);
    if(f->linked) {
        LEGACY_SLOT* slot=&legacySlots[f->slot];
        if((f->filter || f->count || f->touched) && slot->target){InterlockedExchange(&slot->target->recover,TRUE);KeSetEvent(&slot->target->maintenanceEvent,IO_NO_INCREMENT,FALSE);}
        RemoveEntryList(&f->link);f->linked=FALSE;--slot->count;
    }
    if(f->event){ObDereferenceObject(f->event);f->event=NULL;}
    if(f->packets){ExFreePoolWithTag(f->packets,FILTER_TAG);f->packets=NULL;}
    f->filter=0;f->head=f->count=0;WdfWaitLockRelease(gGate);
}
void LegacyClose(WDFFILEOBJECT file) {
    LEGACY_FILE* f=LegacyFile(file);WdfWaitLockAcquire(gLifetime,NULL);
    if(f->counted){f->counted=FALSE;NT_ASSERT(gOpenFiles && legacyOpenFiles);--gOpenFiles;--legacyOpenFiles;if(!gOpenFiles && !gFdos)IcControlDelete();}
    WdfWaitLockRelease(gLifetime);
}
void LegacyCaller(WDFDEVICE device,WDFREQUEST request) {
    WDF_REQUEST_PARAMETERS p;NTSTATUS status;
    WDF_REQUEST_PARAMETERS_INIT(&p);WdfRequestGetParameters(request,&p);
    if(p.Type==WdfRequestTypeDeviceControl && p.Parameters.DeviceIoControl.IoControlCode==IV2_IOCTL(0x810)) {
        ULONGLONG handles[2]={0};PVOID buffer=NULL;PKEVENT event=NULL;LEGACY_FILE* f;WDFFILEOBJECT file;
        SIZE_T length=p.Parameters.DeviceIoControl.InputBufferLength;
        if(length!=(WdfRequestIsFrom32BitProcess(request)?8u:16u) || p.Parameters.DeviceIoControl.OutputBufferLength){WdfRequestComplete(request,STATUS_INVALID_BUFFER_SIZE);return;}
        status=WdfRequestRetrieveInputBuffer(request,length,&buffer,NULL);
        if(NT_SUCCESS(status)){RtlCopyMemory(handles,buffer,length);if(!handles[0] || handles[1])status=STATUS_INVALID_PARAMETER;}
        if(NT_SUCCESS(status))status=ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)handles[0],EVENT_MODIFY_STATE|SYNCHRONIZE,*ExEventObjectType,WdfRequestGetRequestorMode(request),(PVOID*)&event,NULL);
        file=WdfRequestGetFileObject(request);
        if(NT_SUCCESS(status) && !file)status=STATUS_INVALID_HANDLE;
        if(NT_SUCCESS(status)) {
            f=LegacyFile(file);WdfWaitLockAcquire(gGate,NULL);
            if(!f->linked)status=STATUS_FILE_CLOSED;
            else {status=IcTrialAccessLocked();if(NT_SUCCESS(status) && f->slot==19)status=IcTrialCommitLocked();}
            if(NT_SUCCESS(status)){if(f->event)ObDereferenceObject(f->event);f->event=event;event=NULL;if(f->count)KeSetEvent(f->event,IO_NO_INCREMENT,FALSE);else KeClearEvent(f->event);}
            WdfWaitLockRelease(gGate);
        }
        if(event)ObDereferenceObject(event);WdfRequestComplete(request,status);return;
    }
    status=WdfDeviceEnqueueRequest(device,request);if(!NT_SUCCESS(status))WdfRequestComplete(request,status);
}
static NTSTATUS LegacyWriteLocked(LEGACY_FILE* f,PVOID input,SIZE_T size,SIZE_T* written) {
    LEGACY_SLOT* slot=&legacySlots[f->slot];BOOLEAN keyboard=f->slot<10;SIZE_T unit=keyboard?sizeof(KEYBOARD_INPUT_DATA):sizeof(MOUSE_INPUT_DATA);ULONG i,count;NTSTATUS status;
    if(!size || size%unit || size/unit>8192)return STATUS_INVALID_BUFFER_SIZE;
    status=IcTrialSendAccessLocked();if(!NT_SUCCESS(status))return status;
    if(!NT_SUCCESS(f->revoked))return f->revoked;
    if(!slot->target || slot->target->stop || !slot->target->thread || !slot->target->connection.ClassService)return STATUS_DEVICE_NOT_CONNECTED;
    count=(ULONG)(size/unit);
    for(i=0;i<count;++i) {
        if(keyboard){KEYBOARD_INPUT_DATA* k=(KEYBOARD_INPUT_DATA*)input+i;if(k->Reserved || k->MakeCode>255 || (k->Flags&~0x3fu) || (k->Flags&6u)==6u)return STATUS_INVALID_PARAMETER;}
        else {MOUSE_INPUT_DATA* m=(MOUSE_INPUT_DATA*)input+i;if((m->Flags&~0x10fu) || (m->ButtonFlags&~0xfffu))return STATUS_INVALID_PARAMETER;}
    }
    f->touched=TRUE;
    for(i=0;i<count;++i){LEGACY_PACKET packet;LONG routed;RtlZeroMemory(&packet,sizeof(packet));RtlCopyMemory(&packet,(PUCHAR)input+unit*i,unit);
        if(!RecoverLocked(slot->target))break;
        routed=LegacyDispatchLocked(f->slot,f,&packet);
        if(routed<0)break;
        if(!routed && !DeliverLocked(slot->target,&packet))break;
        *written+=unit;
    }
    // The original DLL uses IoStatus.Information as the accepted byte count,
    // including for an IOCTL with a zero-length output buffer.
    return *written?STATUS_SUCCESS:STATUS_DEVICE_BUSY;
}
void LegacyIoctl(WDFQUEUE queue,WDFREQUEST request,SIZE_T outputLength,SIZE_T inputLength,ULONG code) {
    NTSTATUS status=STATUS_INVALID_DEVICE_REQUEST;SIZE_T written=0;PVOID input=NULL,output=NULL;WDFFILEOBJECT file=WdfRequestGetFileObject(request);LEGACY_FILE* f;
    UNREFERENCED_PARAMETER(queue);
    if(!file){WdfRequestComplete(request,STATUS_INVALID_HANDLE);return;}f=LegacyFile(file);
    if(inputLength){status=WdfRequestRetrieveInputBuffer(request,inputLength,&input,NULL);if(!NT_SUCCESS(status))goto done;}
    if(outputLength){status=WdfRequestRetrieveOutputBuffer(request,outputLength,&output,NULL);if(!NT_SUCCESS(status))goto done;}
    WdfWaitLockAcquire(gGate,NULL);IcTrialCheckLocked();
    if(!f->linked){status=STATUS_FILE_CLOSED;goto unlock;}
    if(code==IV2_IOCTL(0x801)) {
        if(inputLength!=sizeof(LONG) || outputLength)status=STATUS_INVALID_BUFFER_SIZE;
        else {RtlCopyMemory(&f->precedence,input,sizeof(LONG));status=STATUS_SUCCESS;}
    }else if(code==IV2_IOCTL(0x802)) {
        if(inputLength || outputLength<sizeof(LONG))status=STATUS_INVALID_BUFFER_SIZE;
        else {RtlCopyMemory(output,&f->precedence,sizeof(LONG));written=sizeof(LONG);status=STATUS_SUCCESS;}
    }else if(code==IV2_IOCTL(0x804)) {
        USHORT filter=0;
        if(inputLength!=sizeof(filter) || outputLength)status=STATUS_INVALID_BUFFER_SIZE;
        else {RtlCopyMemory(&filter,input,sizeof(filter));status=STATUS_SUCCESS;
            if(filter){status=IcTrialCommitLocked();if(NT_SUCCESS(status) && (gOwners[0]||gOwners[1]))status=STATUS_DEVICE_BUSY;
                if(NT_SUCCESS(status) && !NT_SUCCESS(f->revoked))status=f->revoked;
                if(NT_SUCCESS(status) && !f->event)status=STATUS_DEVICE_NOT_READY;
                if(NT_SUCCESS(status) && !f->packets){f->packets=ExAllocatePoolZero(NonPagedPoolNx,IV2_LEGACY_CAPACITY*sizeof(LEGACY_PACKET),FILTER_TAG);if(!f->packets)status=STATUS_INSUFFICIENT_RESOURCES;}}
            if(NT_SUCCESS(status))f->filter=filter;
        }
    }else if(code==IV2_IOCTL(0x808)) {
        if(inputLength || outputLength<sizeof(USHORT))status=STATUS_INVALID_BUFFER_SIZE;
        else {RtlCopyMemory(output,&f->filter,sizeof(USHORT));written=sizeof(USHORT);status=STATUS_SUCCESS;}
    }else if(code==IV2_IOCTL(0x880)) {
        DEVICE_CONTEXT* target=legacySlots[f->slot].target;
        if(inputLength)status=STATUS_INVALID_BUFFER_SIZE;
        else if(!target || target->stop)status=STATUS_DEVICE_NOT_CONNECTED;
        else {ULONG n=0;while(n<255 && target->info.hardware_id[n])++n;written=(n+1)*sizeof(USHORT);if(outputLength<written){written=0;status=STATUS_BUFFER_TOO_SMALL;}else{RtlCopyMemory(output,target->info.hardware_id,written);status=STATUS_SUCCESS;}}
    }else if(code==IV2_IOCTL(0x840)) {
        SIZE_T unit=f->slot<10?sizeof(KEYBOARD_INPUT_DATA):sizeof(MOUSE_INPUT_DATA);ULONG count,i;
        status=IcTrialAccessLocked();if(NT_SUCCESS(status) && !NT_SUCCESS(f->revoked))status=f->revoked;
        if(!NT_SUCCESS(status)){if(f->event)KeClearEvent(f->event);goto unlock;}
        if(inputLength || !outputLength || outputLength%unit || outputLength/unit>8192)status=STATUS_INVALID_BUFFER_SIZE;
        else {count=min((ULONG)(outputLength/unit),f->count);for(i=0;i<count;++i){RtlCopyMemory((PUCHAR)output+unit*i,&f->packets[f->head],unit);f->head=(f->head+1)%IV2_LEGACY_CAPACITY;--f->count;}written=count*unit;if(!f->count && f->event)KeClearEvent(f->event);status=STATUS_SUCCESS;}
    }else if(code==IV2_IOCTL(0x820)) {
        if(outputLength)status=STATUS_INVALID_BUFFER_SIZE;else status=LegacyWriteLocked(f,input,inputLength,&written);
    }
unlock:WdfWaitLockRelease(gGate);
done:WdfRequestCompleteWithInformation(request,status,written);
}
static void LegacyDelete(void) {ULONG i;for(i=0;i<IV2_LEGACY_SLOTS;++i)if(legacySlots[i].endpoint){WDFDEVICE d=legacySlots[i].endpoint;legacySlots[i].endpoint=NULL;WdfObjectDelete(d);}}
static NTSTATUS LegacyInitialize(WDFDRIVER driver) {
    ULONG i;NTSTATUS status=STATUS_SUCCESS;
    DECLARE_CONST_UNICODE_STRING(sddl,L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    for(i=0;i<IV2_LEGACY_SLOTS;++i) {
        PWDFDEVICE_INIT init;WDF_OBJECT_ATTRIBUTES attributes;WDF_FILEOBJECT_CONFIG file;WDF_IO_QUEUE_CONFIG queue;WCHAR name[64],link[64];UNICODE_STRING n,l;
        RtlStringCchPrintfW(name,RTL_NUMBER_OF(name),L"\\Device\\interception%02u",i);RtlStringCchPrintfW(link,RTL_NUMBER_OF(link),L"\\DosDevices\\interception%02u",i);
        RtlInitUnicodeString(&n,name);RtlInitUnicodeString(&l,link);
        init=WdfControlDeviceInitAllocate(driver,&sddl);if(!init){status=STATUS_INSUFFICIENT_RESOURCES;break;}
        WdfDeviceInitSetDeviceType(init,FILE_DEVICE_UNKNOWN);WdfDeviceInitSetCharacteristics(init,FILE_DEVICE_SECURE_OPEN,FALSE);WdfDeviceInitSetExclusive(init,FALSE);
        status=WdfDeviceInitAssignName(init,&n);if(!NT_SUCCESS(status)){WdfDeviceInitFree(init);break;}
        WdfDeviceInitSetIoInCallerContextCallback(init,LegacyCaller);
        WDF_FILEOBJECT_CONFIG_INIT(&file,LegacyCreate,LegacyClose,LegacyCleanup);
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,LEGACY_FILE);attributes.ExecutionLevel=WdfExecutionLevelPassive;
        WdfDeviceInitSetFileObjectConfig(init,&file,&attributes);
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,LEGACY_ENDPOINT);attributes.ExecutionLevel=WdfExecutionLevelPassive;attributes.SynchronizationScope=WdfSynchronizationScopeNone;
        status=WdfDeviceCreate(&init,&attributes,&legacySlots[i].endpoint);if(!NT_SUCCESS(status)){if(init)WdfDeviceInitFree(init);break;}
        LegacyEndpoint(legacySlots[i].endpoint)->slot=i;
        status=WdfDeviceCreateSymbolicLink(legacySlots[i].endpoint,&l);if(!NT_SUCCESS(status))break;
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue,WdfIoQueueDispatchParallel);queue.PowerManaged=WdfFalse;queue.EvtIoDeviceControl=LegacyIoctl;
        status=WdfIoQueueCreate(legacySlots[i].endpoint,&queue,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);if(!NT_SUCCESS(status))break;
    }
    if(!NT_SUCCESS(status)){LegacyDelete();return status;}
    for(i=0;i<IV2_LEGACY_SLOTS;++i)WdfControlFinishInitializing(legacySlots[i].endpoint);
    return STATUS_SUCCESS;
}
