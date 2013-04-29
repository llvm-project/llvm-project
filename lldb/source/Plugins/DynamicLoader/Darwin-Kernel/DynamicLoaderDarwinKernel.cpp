//===-- DynamicLoaderDarwinKernel.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-python.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/DataBuffer.h"
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/State.h"
#include "lldb/Host/Symbols.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"


#include "DynamicLoaderDarwinKernel.h"

//#define ENABLE_DEBUG_PRINTF // COMMENT THIS LINE OUT PRIOR TO CHECKIN
#ifdef ENABLE_DEBUG_PRINTF
#include <stdio.h>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

using namespace lldb;
using namespace lldb_private;

// Progressively greater amounts of scanning we will allow
// For some targets very early in startup, we can't do any random reads of memory or we can crash the device
// so a setting is needed that can completely disable the KASLR scans.

enum KASLRScanType
{
    eKASLRScanNone = 0,         // No reading into the inferior at all
    eKASLRScanLowgloAddresses,  // Check one word of memory for a possible kernel addr, then see if a kernel is there
    eKASLRScanNearPC,           // Scan backwards from the current $pc looking for kernel; checking at 96 locations total
    eKASLRScanExhaustiveScan    // Scan through the entire possible kernel address range looking for a kernel
};

OptionEnumValueElement
g_kaslr_kernel_scan_enum_values[] =
{
    { eKASLRScanNone,            "none",            "Do not read memory looking for a Darwin kernel when attaching." },
    { eKASLRScanLowgloAddresses, "basic",           "Check for the Darwin kernel's load addr in the lowglo page (boot-args=debug) only." },
    { eKASLRScanNearPC,          "fast-scan",       "Scan near the pc value on attach to find the Darwin kernel's load address."},
    { eKASLRScanExhaustiveScan,  "exhaustive-scan", "Scan through the entire potential address range of Darwin kernel (only on 32-bit targets)."},
    { 0, NULL, NULL }
};

static PropertyDefinition
g_properties[] =
{
    { "load-kexts" , OptionValue::eTypeBoolean, true, true, NULL, NULL, "Automatically loads kext images when attaching to a kernel." },
    { "scan-type",   OptionValue::eTypeEnum,    true, eKASLRScanNearPC, NULL, g_kaslr_kernel_scan_enum_values, "Control how many reads lldb will make while searching for a Darwin kernel on attach." },
    {  NULL        , OptionValue::eTypeInvalid, false, 0  , NULL, NULL, NULL  }
};

enum {
    ePropertyLoadKexts,
    ePropertyScanType
};

class DynamicLoaderDarwinKernelProperties : public Properties
{
public:
    
    static ConstString &
    GetSettingName ()
    {
        static ConstString g_setting_name("darwin-kernel");
        return g_setting_name;
    }

    DynamicLoaderDarwinKernelProperties() :
        Properties ()
    {
        m_collection_sp.reset (new OptionValueProperties(GetSettingName()));
        m_collection_sp->Initialize(g_properties);
    }

    virtual
    ~DynamicLoaderDarwinKernelProperties()
    {
    }

    bool
    GetLoadKexts() const
    {
        const uint32_t idx = ePropertyLoadKexts;
        return m_collection_sp->GetPropertyAtIndexAsBoolean (NULL, idx, g_properties[idx].default_uint_value != 0);
    }

    KASLRScanType
    GetScanType() const
    {
        const uint32_t idx = ePropertyScanType;
        return (KASLRScanType) m_collection_sp->GetPropertyAtIndexAsEnumeration (NULL, idx, g_properties[idx].default_uint_value);
    }


};

typedef std::shared_ptr<DynamicLoaderDarwinKernelProperties> DynamicLoaderDarwinKernelPropertiesSP;

static const DynamicLoaderDarwinKernelPropertiesSP &
GetGlobalProperties()
{
    static DynamicLoaderDarwinKernelPropertiesSP g_settings_sp;
    if (!g_settings_sp)
        g_settings_sp.reset (new DynamicLoaderDarwinKernelProperties ());
    return g_settings_sp;
}

//----------------------------------------------------------------------
// Create an instance of this class. This function is filled into
// the plugin info class that gets handed out by the plugin factory and
// allows the lldb to instantiate an instance of this class.
//----------------------------------------------------------------------
DynamicLoader *
DynamicLoaderDarwinKernel::CreateInstance (Process* process, bool force)
{
    if (!force)
    {
        // If the user provided an executable binary and it is not a kernel,
        // this plugin should not create an instance.
        Module* exe_module = process->GetTarget().GetExecutableModulePointer();
        if (exe_module)
        {
            ObjectFile *object_file = exe_module->GetObjectFile();
            if (object_file)
            {
                if (object_file->GetStrata() != ObjectFile::eStrataKernel)
                {
                    return NULL;
                }
            }
        }

        // If the target's architecture does not look like an Apple environment,
        // this plugin should not create an instance.
        const llvm::Triple &triple_ref = process->GetTarget().GetArchitecture().GetTriple();
        switch (triple_ref.getOS())
        {
            case llvm::Triple::Darwin:
            case llvm::Triple::MacOSX:
            case llvm::Triple::IOS:
                if (triple_ref.getVendor() != llvm::Triple::Apple)
                {
                   return NULL;
                }
                break;
            // If we have triple like armv7-unknown-unknown, we should try looking for a Darwin kernel.
            case llvm::Triple::UnknownOS:
                break;
            default:
                return NULL;
                break;
        }
    }

    // At this point if there is an ExecutableModule, it is a kernel and the Target is some variant of an Apple system.  
    // If the Process hasn't provided the kernel load address, we need to look around in memory to find it.

    addr_t kernel_load_address = SearchForDarwinKernel (process);
    if (kernel_load_address != LLDB_INVALID_ADDRESS)
    {
        process->SetCanJIT(false);
        return new DynamicLoaderDarwinKernel (process, kernel_load_address);
    }
    return NULL;
}

lldb::addr_t
DynamicLoaderDarwinKernel::SearchForDarwinKernel (Process *process)
{
    addr_t kernel_load_address = process->GetImageInfoAddress();
    if (kernel_load_address == LLDB_INVALID_ADDRESS)
    {
        kernel_load_address = SearchForKernelAtSameLoadAddr (process);
        if (kernel_load_address == LLDB_INVALID_ADDRESS)
        {
            kernel_load_address = SearchForKernelWithDebugHints (process);
            if (kernel_load_address == LLDB_INVALID_ADDRESS)
            {
                kernel_load_address = SearchForKernelNearPC (process);
                if (kernel_load_address == LLDB_INVALID_ADDRESS)
                {
                    kernel_load_address = SearchForKernelViaExhaustiveSearch (process);
                }
            }
        }
    }
    return kernel_load_address;
}

//----------------------------------------------------------------------
// Check if the kernel binary is loaded in memory without a slide.
// First verify that the ExecutableModule is a kernel before we proceed.
// Returns the address of the kernel if one was found, else LLDB_INVALID_ADDRESS.
//----------------------------------------------------------------------
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelAtSameLoadAddr (Process *process)
{
    Module *exe_module = process->GetTarget().GetExecutableModulePointer();
    if (exe_module == NULL)
        return LLDB_INVALID_ADDRESS;

    ObjectFile *exe_objfile = exe_module->GetObjectFile();
    if (exe_objfile == NULL)
        return LLDB_INVALID_ADDRESS;

    if (exe_objfile->GetType() != ObjectFile::eTypeExecutable || exe_objfile->GetStrata() != ObjectFile::eStrataKernel)
        return LLDB_INVALID_ADDRESS;

    if (!exe_objfile->GetHeaderAddress().IsValid())
        return LLDB_INVALID_ADDRESS;

    if (CheckForKernelImageAtAddress (exe_objfile->GetHeaderAddress().GetFileAddress(), process) == exe_module->GetUUID())
        return exe_objfile->GetHeaderAddress().GetFileAddress();

    return LLDB_INVALID_ADDRESS;
}

//----------------------------------------------------------------------
// If the debug flag is included in the boot-args nvram setting, the kernel's load address
// will be noted in the lowglo page at a fixed address
// Returns the address of the kernel if one was found, else LLDB_INVALID_ADDRESS.
//----------------------------------------------------------------------
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelWithDebugHints (Process *process)
{
    if (GetGlobalProperties()->GetScanType() == eKASLRScanNone)
        return LLDB_INVALID_ADDRESS;

    Error read_err;
    addr_t addr = LLDB_INVALID_ADDRESS;
    if (process->GetTarget().GetArchitecture().GetAddressByteSize() == 8)
    {
        addr = process->ReadUnsignedIntegerFromMemory (0xffffff8000002010ULL, 8, LLDB_INVALID_ADDRESS, read_err);
    }
    else
    {
        addr = process->ReadUnsignedIntegerFromMemory (0xffff0110, 4, LLDB_INVALID_ADDRESS, read_err);
    }

    if (addr == 0)
        addr = LLDB_INVALID_ADDRESS;

    if (addr != LLDB_INVALID_ADDRESS)
    {
        if (CheckForKernelImageAtAddress (addr, process).IsValid())
            return addr;
    }

    return LLDB_INVALID_ADDRESS;
}

//----------------------------------------------------------------------
// If the kernel is currently executing when lldb attaches, and we don't have
// a better way of finding the kernel's load address, try searching backwards
// from the current pc value looking for the kernel's Mach header in memory.
// Returns the address of the kernel if one was found, else LLDB_INVALID_ADDRESS.
//----------------------------------------------------------------------
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelNearPC (Process *process)
{
    if (GetGlobalProperties()->GetScanType() == eKASLRScanNone 
        || GetGlobalProperties()->GetScanType() == eKASLRScanLowgloAddresses)
    {
        return LLDB_INVALID_ADDRESS;
    }

    ThreadSP thread = process->GetThreadList().GetSelectedThread ();
    if (thread.get() == NULL)
        return LLDB_INVALID_ADDRESS;
    addr_t pc = thread->GetRegisterContext ()->GetPC(LLDB_INVALID_ADDRESS);

    if (pc == LLDB_INVALID_ADDRESS)
        return LLDB_INVALID_ADDRESS;

    addr_t kernel_range_low;
    if (process->GetTarget().GetArchitecture().GetAddressByteSize() == 8)
    {
        kernel_range_low = 1ULL << 63;
    }
    else
    {
        kernel_range_low = 1ULL << 31;
    }

    // Outside the normal kernel address range, this is probably userland code running right now
    if (pc < kernel_range_low)
        return LLDB_INVALID_ADDRESS;

    // The kernel will load at at one megabyte boundary (0x100000), or at that boundary plus 
    // an offset of one page (0x1000) or two, depending on the device.

    // Round the current pc down to the nearest one megabyte boundary - the place where we will start searching.
    addr_t addr = pc & ~0xfffff;

    int i = 0;
    while (i < 32 && pc >= kernel_range_low)
    {
        if (CheckForKernelImageAtAddress (addr, process).IsValid())
            return addr;
        if (CheckForKernelImageAtAddress (addr + 0x1000, process).IsValid())
            return addr + 0x1000;
        if (CheckForKernelImageAtAddress (addr + 0x2000, process).IsValid())
            return addr + 0x2000;
        i++;
        addr -= 0x100000;
    }

    return LLDB_INVALID_ADDRESS;
}

//----------------------------------------------------------------------
// Scan through the valid address range for a kernel binary.
// This is uselessly slow in 64-bit environments so we don't even try it.
// This scan is not enabled by default even for 32-bit targets.
// Returns the address of the kernel if one was found, else LLDB_INVALID_ADDRESS.
//----------------------------------------------------------------------
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelViaExhaustiveSearch (Process *process)
{
    if (GetGlobalProperties()->GetScanType() != eKASLRScanExhaustiveScan)
    {
        return LLDB_INVALID_ADDRESS;
    }

    addr_t kernel_range_low, kernel_range_high;
    if (process->GetTarget().GetArchitecture().GetAddressByteSize() == 8)
    {
        kernel_range_low = 1ULL << 63;
        kernel_range_high = UINT64_MAX;
    }
    else
    {
        kernel_range_low = 1ULL << 31;
        kernel_range_high = UINT32_MAX;
    }

    // Stepping through memory at one-megabyte resolution looking for a kernel
    // rarely works (fast enough) with a 64-bit address space -- for now, let's
    // not even bother.  We may be attaching to something which *isn't* a kernel
    // and we don't want to spin for minutes on-end looking for a kernel.
    if (process->GetTarget().GetArchitecture().GetAddressByteSize() == 8)
        return LLDB_INVALID_ADDRESS;

    addr_t addr = kernel_range_low;

    while (addr >= kernel_range_low && addr < kernel_range_high)
    {
        if (CheckForKernelImageAtAddress (addr, process).IsValid())
            return addr;
        if (CheckForKernelImageAtAddress (addr + 0x1000, process).IsValid())
            return addr + 0x1000;
        if (CheckForKernelImageAtAddress (addr + 0x2000, process).IsValid())
            return addr + 0x2000;
        addr += 0x100000;
    }
    return LLDB_INVALID_ADDRESS;
}

//----------------------------------------------------------------------
// Given an address in memory, look to see if there is a kernel image at that
// address.  
// Returns a UUID; if a kernel was not found at that address, UUID.IsValid() will be false.
//----------------------------------------------------------------------
lldb_private::UUID
DynamicLoaderDarwinKernel::CheckForKernelImageAtAddress (lldb::addr_t addr, Process *process)
{
    if (addr == LLDB_INVALID_ADDRESS)
        return UUID();

    // First try a quick test -- read the first 4 bytes and see if there is a valid Mach-O magic field there
    // (the first field of the mach_header/mach_header_64 struct).

    Error read_error;
    uint64_t result = process->ReadUnsignedIntegerFromMemory (addr, 4, LLDB_INVALID_ADDRESS, read_error);
    if (result != llvm::MachO::HeaderMagic64
        && result != llvm::MachO::HeaderMagic32
        && result != llvm::MachO::HeaderMagic32Swapped 
        && result != llvm::MachO::HeaderMagic64Swapped)
    {
        return UUID();
    }

    // Read the mach header and see whether it looks like a kernel
    llvm::MachO::mach_header header;
    if (process->DoReadMemory (addr, &header, sizeof(header), read_error) != sizeof(header))
        return UUID();

    if (header.magic == llvm::MachO::HeaderMagic32Swapped ||
        header.magic == llvm::MachO::HeaderMagic64Swapped)
    {
        header.magic        = llvm::ByteSwap_32(header.magic);
        header.cputype      = llvm::ByteSwap_32(header.cputype);
        header.cpusubtype   = llvm::ByteSwap_32(header.cpusubtype);
        header.filetype     = llvm::ByteSwap_32(header.filetype);
        header.ncmds        = llvm::ByteSwap_32(header.ncmds);
        header.sizeofcmds   = llvm::ByteSwap_32(header.sizeofcmds);
        header.flags        = llvm::ByteSwap_32(header.flags);
    }

    // A kernel is an executable which does not have the dynamic link object flag set.
    if (header.filetype == llvm::MachO::HeaderFileTypeExecutable
        && (header.flags & llvm::MachO::HeaderFlagBitIsDynamicLinkObject) == 0)
    {
        // Create a full module to get the UUID
        ModuleSP memory_module_sp = process->ReadModuleFromMemory (FileSpec ("temp_mach_kernel", false), addr);
        if (!memory_module_sp.get())
            return UUID();

        ObjectFile *exe_objfile = memory_module_sp->GetObjectFile();
        if (exe_objfile == NULL)
            return UUID();

        if (exe_objfile->GetType() == ObjectFile::eTypeExecutable && exe_objfile->GetStrata() == ObjectFile::eStrataKernel)
        {
            return memory_module_sp->GetUUID();
        }
    }

    return UUID();
}

//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
DynamicLoaderDarwinKernel::DynamicLoaderDarwinKernel (Process* process, lldb::addr_t kernel_addr) :
    DynamicLoader(process),
    m_kernel_load_address (kernel_addr),
    m_kernel(),
    m_kext_summary_header_ptr_addr (),
    m_kext_summary_header_addr (),
    m_kext_summary_header (),
    m_known_kexts (),
    m_mutex(Mutex::eMutexTypeRecursive),
    m_break_id (LLDB_INVALID_BREAK_ID)
{
    PlatformSP platform_sp(Platform::FindPlugin (process, "darwin-kernel"));
    // Only select the darwin-kernel Platform if we've been asked to load kexts.
    // It can take some time to scan over all of the kext info.plists and that
    // shouldn't be done if kext loading is explicitly disabled.
    if (platform_sp.get() && GetGlobalProperties()->GetLoadKexts())
    {
        process->GetTarget().SetPlatform (platform_sp);
    }
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
DynamicLoaderDarwinKernel::~DynamicLoaderDarwinKernel()
{
    Clear(true);
}

void
DynamicLoaderDarwinKernel::UpdateIfNeeded()
{
    LoadKernelModuleIfNeeded();
    SetNotificationBreakpointIfNeeded ();
}
//------------------------------------------------------------------
/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
//------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::DidAttach ()
{
    PrivateInitialize(m_process);
    UpdateIfNeeded();
}

//------------------------------------------------------------------
/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
//------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::DidLaunch ()
{
    PrivateInitialize(m_process);
    UpdateIfNeeded();
}


//----------------------------------------------------------------------
// Clear out the state of this class.
//----------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::Clear (bool clear_process)
{
    Mutex::Locker locker(m_mutex);

    if (m_process->IsAlive() && LLDB_BREAK_ID_IS_VALID(m_break_id))
        m_process->ClearBreakpointSiteByID(m_break_id);

    if (clear_process)
        m_process = NULL;
    m_kernel.Clear();
    m_known_kexts.clear();
    m_kext_summary_header_ptr_addr.Clear();
    m_kext_summary_header_addr.Clear();
    m_break_id = LLDB_INVALID_BREAK_ID;
}


bool
DynamicLoaderDarwinKernel::KextImageInfo::LoadImageAtFileAddress (Process *process)
{
    if (IsLoaded())
        return true;

    if (m_module_sp)
    {
        bool changed = false;
        if (m_module_sp->SetLoadAddress (process->GetTarget(), 0, changed))
            m_load_process_stop_id = process->GetStopID();
    }
    return false;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetModule (ModuleSP module_sp)
{
    m_module_sp = module_sp;
    if (module_sp.get() && module_sp->GetObjectFile())
    {
        if (module_sp->GetObjectFile()->GetType() == ObjectFile::eTypeExecutable
            && module_sp->GetObjectFile()->GetStrata() == ObjectFile::eStrataKernel)
        {
            m_kernel_image = true;
        }
        else
        {
            m_kernel_image = false;
        }
    }
}

ModuleSP
DynamicLoaderDarwinKernel::KextImageInfo::GetModule ()
{
    return m_module_sp;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetLoadAddress (addr_t load_addr)
{ 
    m_load_address = load_addr;
}

addr_t
DynamicLoaderDarwinKernel::KextImageInfo::GetLoadAddress () const
{ 
    return m_load_address;
}

uint64_t 
DynamicLoaderDarwinKernel::KextImageInfo::GetSize () const
{
    return m_size;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetSize (uint64_t size)
{
    m_size = size;
}

uint32_t
DynamicLoaderDarwinKernel::KextImageInfo::GetProcessStopId () const
{
    return m_load_process_stop_id;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetProcessStopId (uint32_t stop_id)
{
    m_load_process_stop_id = stop_id;
}

bool
DynamicLoaderDarwinKernel::KextImageInfo::operator== (const KextImageInfo &rhs)
{
    if (m_uuid.IsValid() || rhs.GetUUID().IsValid())
    {
        if (m_uuid == rhs.GetUUID())
        {
            return true;
        }
        return false;
    }

    if (m_name == rhs.GetName() && m_load_address == rhs.GetLoadAddress())
        return true;

    return false;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetName (const char *name)
{
    m_name = name;
}

std::string
DynamicLoaderDarwinKernel::KextImageInfo::GetName () const
{
    return m_name;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetUUID (const UUID &uuid)
{
    m_uuid = uuid;
}

UUID
DynamicLoaderDarwinKernel::KextImageInfo::GetUUID () const
{
    return m_uuid;
}

// Given the m_load_address from the kext summaries, and a UUID, try to create an in-memory
// Module at that address.  Require that the MemoryModule have a matching UUID and detect
// if this MemoryModule is a kernel or a kext.
//
// Returns true if m_memory_module_sp is now set to a valid Module.  

bool
DynamicLoaderDarwinKernel::KextImageInfo::ReadMemoryModule (Process *process)
{
    if (m_memory_module_sp.get() != NULL)
        return true;
    if (m_load_address == LLDB_INVALID_ADDRESS)
        return false;

    FileSpec file_spec;
    file_spec.SetFile (m_name.c_str(), false);

    ModuleSP memory_module_sp = process->ReadModuleFromMemory (file_spec, m_load_address);

    if (memory_module_sp.get() == NULL)
        return false;

    bool is_kernel = false;
    if (memory_module_sp->GetObjectFile())
    {
        if (memory_module_sp->GetObjectFile()->GetType() == ObjectFile::eTypeExecutable
            && memory_module_sp->GetObjectFile()->GetStrata() == ObjectFile::eStrataKernel)
        {
            is_kernel = true;
        }
        else if (memory_module_sp->GetObjectFile()->GetType() == ObjectFile::eTypeSharedLibrary)
        {
            is_kernel = false;
        }
    }

    // If this is a kext, and the kernel specified what UUID we should find at this 
    // load address, require that the memory module have a matching UUID or something 
    // has gone wrong and we should discard it.
    if (m_uuid.IsValid())
    {
        if (m_uuid != memory_module_sp->GetUUID())
        {
            return false;
        }
    }

    // If the in-memory Module has a UUID, let's use that.
    if (!m_uuid.IsValid() && memory_module_sp->GetUUID().IsValid())
    {
        m_uuid = memory_module_sp->GetUUID();
    }

    m_memory_module_sp = memory_module_sp;
    m_kernel_image = is_kernel;
    if (is_kernel)
    {
        if (memory_module_sp->GetArchitecture().IsValid())
        {
            process->GetTarget().SetArchitecture(memory_module_sp->GetArchitecture());
        }
        if (m_uuid.IsValid())
        {
            Module* exe_module = process->GetTarget().GetExecutableModulePointer();
            if (exe_module && exe_module->GetUUID().IsValid())
            {
                if (m_uuid != exe_module->GetUUID())
                {
                    Stream *s = &process->GetTarget().GetDebugger().GetOutputStream();
                    if (s)
                    {
                        char memory_module_uuidbuf[64];
                        char exe_module_uuidbuf[64];
                        s->Printf ("warning: Host-side kernel file has Mach-O UUID of %s but remote kernel has a UUID of %s -- a mismatched kernel file will result in a poor debugger experience.\n", 
                                   exe_module->GetUUID().GetAsCString(exe_module_uuidbuf, sizeof (exe_module_uuidbuf)),
                                   m_uuid.GetAsCString(memory_module_uuidbuf, sizeof (memory_module_uuidbuf)));
                        s->Flush ();
                    }
                }
            }
        }
    }

    return true;
}

bool
DynamicLoaderDarwinKernel::KextImageInfo::IsKernel () const
{
    return m_kernel_image == true;
}

void
DynamicLoaderDarwinKernel::KextImageInfo::SetIsKernel (bool is_kernel) 
{
    m_kernel_image = is_kernel;
}

bool
DynamicLoaderDarwinKernel::KextImageInfo::LoadImageUsingMemoryModule (Process *process)
{
    if (IsLoaded())
        return true;


    Target &target = process->GetTarget();

    // If we don't have / can't create a memory module for this kext, don't try to load it - we won't
    // have the correct segment load addresses.
    if (!ReadMemoryModule (process))
    {
        return false;
    }

    bool uuid_is_valid = m_uuid.IsValid();

    if (IsKernel() && uuid_is_valid && m_memory_module_sp.get())
    {
        Stream *s = &target.GetDebugger().GetOutputStream();
        if (s)
        {
            char uuidbuf[64];
            s->Printf ("Kernel UUID: %s\n", m_memory_module_sp->GetUUID().GetAsCString(uuidbuf, sizeof (uuidbuf)));
            s->Printf ("Load Address: 0x%" PRIx64 "\n", m_load_address);
        }
    }

    if (!m_module_sp)
    {
        // See if the kext has already been loaded into the target, probably by the user doing target modules add.
        const ModuleList &target_images = target.GetImages();
        m_module_sp = target_images.FindModule(m_uuid);

        // Search for the kext on the local filesystem via the UUID
        if (!m_module_sp && uuid_is_valid)
        {
            ModuleSpec module_spec;
            module_spec.GetUUID() = m_uuid;
            module_spec.GetArchitecture() = target.GetArchitecture();

            // For the kernel, we really do need an on-disk file copy of the binary to do anything useful.
            // This will force a clal to 
            if (IsKernel())
            {
                if (Symbols::DownloadObjectAndSymbolFile (module_spec, true))
                {
                    if (module_spec.GetFileSpec().Exists())
                    {
                        m_module_sp.reset(new Module (module_spec.GetFileSpec(), target.GetArchitecture()));
                        if (m_module_sp.get() && m_module_sp->MatchesModuleSpec (module_spec))
                        {
                            ModuleList loaded_module_list;
                            loaded_module_list.Append (m_module_sp);
                            target.ModulesDidLoad (loaded_module_list);
                        }
                    }
                }
            }

            // If the current platform is PlatformDarwinKernel, create a ModuleSpec with the filename set 
            // to be the bundle ID for this kext, e.g. "com.apple.filesystems.msdosfs", and ask the platform
            // to find it.
            PlatformSP platform_sp (target.GetPlatform());
            if (platform_sp)
            {
                const char *pname = platform_sp->GetShortPluginName();
                if (pname && strcmp (pname, "darwin-kernel") == 0)
                {
                    ModuleSpec kext_bundle_module_spec(module_spec);
                    FileSpec kext_filespec(m_name.c_str(), false);
                    kext_bundle_module_spec.GetFileSpec() = kext_filespec;
                    platform_sp->GetSharedModule (kext_bundle_module_spec, m_module_sp, &target.GetExecutableSearchPaths(), NULL, NULL);
                }
            }

            // Ask the Target to find this file on the local system, if possible.
            // This will search in the list of currently-loaded files, look in the 
            // standard search paths on the system, and on a Mac it will try calling
            // the DebugSymbols framework with the UUID to find the binary via its
            // search methods.
            if (!m_module_sp)
            {
                m_module_sp = target.GetSharedModule (module_spec);
            }

            if (IsKernel() && !m_module_sp)
            {
                Stream *s = &target.GetDebugger().GetOutputStream();
                if (s)
                {
                    s->Printf ("WARNING: Unable to locate kernel binary on this system.\n");
                }
            }
        }

        // If we managed to find a module, append it to the target's list of images.
        // If we also have a memory module, require that they have matching UUIDs
        if (m_module_sp)
        {
            bool uuid_match_ok = true;
            if (m_memory_module_sp)
            {
                if (m_module_sp->GetUUID() != m_memory_module_sp->GetUUID())
                {
                    uuid_match_ok = false;
                }
            }
            if (uuid_match_ok)
            {
                target.GetImages().AppendIfNeeded(m_module_sp);
                if (IsKernel() && target.GetExecutableModulePointer() != m_module_sp.get())
                {
                    target.SetExecutableModule (m_module_sp, false);
                }
            }
        }
    }
    
    if (!m_module_sp && !IsKernel() && m_uuid.IsValid() && !m_name.empty())
    {
        Stream *s = &target.GetDebugger().GetOutputStream();
        if (s)
        {
            char uuidbuf[64];
            s->Printf ("warning: Can't find binary/dSYM for %s (%s)\n", 
                       m_name.c_str(), m_uuid.GetAsCString(uuidbuf, sizeof (uuidbuf)));
        }
    }

    static ConstString g_section_name_LINKEDIT ("__LINKEDIT");

    if (m_memory_module_sp && m_module_sp)
    {
        if (m_module_sp->GetUUID() == m_memory_module_sp->GetUUID())
        {
            ObjectFile *ondisk_object_file = m_module_sp->GetObjectFile();
            ObjectFile *memory_object_file = m_memory_module_sp->GetObjectFile();
            
            if (memory_object_file && ondisk_object_file)
            {
                // The memory_module for kexts may have an invalid __LINKEDIT seg; skip it.
                const bool ignore_linkedit = !IsKernel ();
                
                SectionList *ondisk_section_list = ondisk_object_file->GetSectionList ();
                SectionList *memory_section_list = memory_object_file->GetSectionList ();
                if (memory_section_list && ondisk_section_list)
                {
                    const uint32_t num_ondisk_sections = ondisk_section_list->GetSize();
                    // There may be CTF sections in the memory image so we can't
                    // always just compare the number of sections (which are actually
                    // segments in mach-o parlance)
                    uint32_t sect_idx = 0;
                    
                    // Use the memory_module's addresses for each section to set the 
                    // file module's load address as appropriate.  We don't want to use
                    // a single slide value for the entire kext - different segments may
                    // be slid different amounts by the kext loader.

                    uint32_t num_sections_loaded = 0;
                    for (sect_idx=0; sect_idx<num_ondisk_sections; ++sect_idx)
                    {
                        SectionSP ondisk_section_sp(ondisk_section_list->GetSectionAtIndex(sect_idx));
                        if (ondisk_section_sp)
                        {
                            // Don't ever load __LINKEDIT as it may or may not be actually
                            // mapped into memory and there is no current way to tell.
                            // I filed rdar://problem/12851706 to track being able to tell
                            // if the __LINKEDIT is actually mapped, but until then, we need
                            // to not load the __LINKEDIT
                            if (ignore_linkedit && ondisk_section_sp->GetName() == g_section_name_LINKEDIT)
                                continue;

                            const Section *memory_section = memory_section_list->FindSectionByName(ondisk_section_sp->GetName()).get();
                            if (memory_section)
                            {
                                target.GetSectionLoadList().SetSectionLoadAddress (ondisk_section_sp, memory_section->GetFileAddress());
                                ++num_sections_loaded;
                            }
                        }
                    }
                    if (num_sections_loaded > 0)
                        m_load_process_stop_id = process->GetStopID();
                    else
                        m_module_sp.reset(); // No sections were loaded
                }
                else
                    m_module_sp.reset(); // One or both section lists
            }
            else
                m_module_sp.reset(); // One or both object files missing
        }
        else
            m_module_sp.reset(); // UUID mismatch
    }

    bool is_loaded = IsLoaded();
    
    if (is_loaded && m_module_sp && IsKernel())
    {
        Stream *s = &target.GetDebugger().GetOutputStream();
        if (s)
        {
            ObjectFile *kernel_object_file = m_module_sp->GetObjectFile();
            if (kernel_object_file)
            {
                addr_t file_address = kernel_object_file->GetHeaderAddress().GetFileAddress();
                if (m_load_address != LLDB_INVALID_ADDRESS && file_address != LLDB_INVALID_ADDRESS)
                {
                    s->Printf ("Kernel slid 0x%" PRIx64 " in memory.\n", m_load_address - file_address);
                }
            }
            {
                s->Printf ("Loaded kernel file %s\n",
                           m_module_sp->GetFileSpec().GetPath().c_str());
            }
            s->Flush ();
        }
    }
    return is_loaded;
}

uint32_t
DynamicLoaderDarwinKernel::KextImageInfo::GetAddressByteSize ()
{
    if (m_memory_module_sp)
        return m_memory_module_sp->GetArchitecture().GetAddressByteSize();
    if (m_module_sp)
        return m_module_sp->GetArchitecture().GetAddressByteSize();
    return 0;
}

lldb::ByteOrder
DynamicLoaderDarwinKernel::KextImageInfo::GetByteOrder()
{
    if (m_memory_module_sp)
        return m_memory_module_sp->GetArchitecture().GetByteOrder();
    if (m_module_sp)
        return m_module_sp->GetArchitecture().GetByteOrder();
    return lldb::endian::InlHostByteOrder();
}

lldb_private::ArchSpec
DynamicLoaderDarwinKernel::KextImageInfo::GetArchitecture () const
{
    if (m_memory_module_sp)
        return m_memory_module_sp->GetArchitecture();
    if (m_module_sp)
        return m_module_sp->GetArchitecture();
    return lldb_private::ArchSpec ();
}


//----------------------------------------------------------------------
// Load the kernel module and initialize the "m_kernel" member. Return
// true _only_ if the kernel is loaded the first time through (subsequent
// calls to this function should return false after the kernel has been
// already loaded).
//----------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::LoadKernelModuleIfNeeded()
{
    if (!m_kext_summary_header_ptr_addr.IsValid())
    {
        m_kernel.Clear();
        m_kernel.SetModule (m_process->GetTarget().GetExecutableModule());
        m_kernel.SetIsKernel(true);

        ConstString kernel_name("mach_kernel");
        if (m_kernel.GetModule().get()
            && m_kernel.GetModule()->GetObjectFile()
            && !m_kernel.GetModule()->GetObjectFile()->GetFileSpec().GetFilename().IsEmpty())
        {
            kernel_name = m_kernel.GetModule()->GetObjectFile()->GetFileSpec().GetFilename();
        }
        m_kernel.SetName (kernel_name.AsCString());

        if (m_kernel.GetLoadAddress() == LLDB_INVALID_ADDRESS)
        {
            m_kernel.SetLoadAddress(m_kernel_load_address);
            if (m_kernel.GetLoadAddress() == LLDB_INVALID_ADDRESS && m_kernel.GetModule())
            {
                // We didn't get a hint from the process, so we will
                // try the kernel at the address that it exists at in
                // the file if we have one
                ObjectFile *kernel_object_file = m_kernel.GetModule()->GetObjectFile();
                if (kernel_object_file)
                {
                    addr_t load_address = kernel_object_file->GetHeaderAddress().GetLoadAddress(&m_process->GetTarget());
                    addr_t file_address = kernel_object_file->GetHeaderAddress().GetFileAddress();
                    if (load_address != LLDB_INVALID_ADDRESS && load_address != 0)
                    {
                        m_kernel.SetLoadAddress (load_address);
                        if (load_address != file_address)
                        {
                            // Don't accidentally relocate the kernel to the File address -- 
                            // the Load address has already been set to its actual in-memory address.  
                            // Mark it as IsLoaded.
                            m_kernel.SetProcessStopId (m_process->GetStopID());
                        }
                    }
                    else
                    {
                        m_kernel.SetLoadAddress(file_address);
                    }
                }
            }
        }
        
        if (m_kernel.GetLoadAddress() != LLDB_INVALID_ADDRESS)
        {
            if (!m_kernel.LoadImageUsingMemoryModule (m_process))
            {
                m_kernel.LoadImageAtFileAddress (m_process);
            }
        }

        if (m_kernel.IsLoaded() && m_kernel.GetModule())
        {
            static ConstString kext_summary_symbol ("gLoadedKextSummaries");
            const Symbol *symbol = m_kernel.GetModule()->FindFirstSymbolWithNameAndType (kext_summary_symbol, eSymbolTypeData);
            if (symbol)
            {
                m_kext_summary_header_ptr_addr = symbol->GetAddress();
                // Update all image infos
                ReadAllKextSummaries ();
            }
        }
        else
        {
            m_kernel.Clear();
        }
    }
}

//----------------------------------------------------------------------
// Static callback function that gets called when our DYLD notification
// breakpoint gets hit. We update all of our image infos and then
// let our super class DynamicLoader class decide if we should stop
// or not (based on global preference).
//----------------------------------------------------------------------
bool
DynamicLoaderDarwinKernel::BreakpointHitCallback (void *baton, 
                                                  StoppointCallbackContext *context, 
                                                  user_id_t break_id, 
                                                  user_id_t break_loc_id)
{    
    return static_cast<DynamicLoaderDarwinKernel*>(baton)->BreakpointHit (context, break_id, break_loc_id);    
}

bool
DynamicLoaderDarwinKernel::BreakpointHit (StoppointCallbackContext *context, 
                                          user_id_t break_id, 
                                          user_id_t break_loc_id)
{    
    Log *log(GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    if (log)
        log->Printf ("DynamicLoaderDarwinKernel::BreakpointHit (...)\n");

    ReadAllKextSummaries ();
    
    if (log)
        PutToLog(log);

    return GetStopWhenImagesChange();
}


bool
DynamicLoaderDarwinKernel::ReadKextSummaryHeader ()
{
    Mutex::Locker locker(m_mutex);

    // the all image infos is already valid for this process stop ID

    if (m_kext_summary_header_ptr_addr.IsValid())
    {
        const uint32_t addr_size = m_kernel.GetAddressByteSize ();
        const ByteOrder byte_order = m_kernel.GetByteOrder();
        Error error;
        // Read enough bytes for a "OSKextLoadedKextSummaryHeader" structure
        // which is currenty 4 uint32_t and a pointer.
        uint8_t buf[24];
        DataExtractor data (buf, sizeof(buf), byte_order, addr_size);
        const size_t count = 4 * sizeof(uint32_t) + addr_size;
        const bool prefer_file_cache = false;
        if (m_process->GetTarget().ReadPointerFromMemory (m_kext_summary_header_ptr_addr, 
                                                          prefer_file_cache,
                                                          error,
                                                          m_kext_summary_header_addr))
        {
            // We got a valid address for our kext summary header and make sure it isn't NULL
            if (m_kext_summary_header_addr.IsValid() && 
                m_kext_summary_header_addr.GetFileAddress() != 0)
            {
                const size_t bytes_read = m_process->GetTarget().ReadMemory (m_kext_summary_header_addr, prefer_file_cache, buf, count, error);
                if (bytes_read == count)
                {
                    lldb::offset_t offset = 0;
                    m_kext_summary_header.version = data.GetU32(&offset);
                    if (m_kext_summary_header.version >= 2)
                    {
                        m_kext_summary_header.entry_size = data.GetU32(&offset);
                    }
                    else
                    {
                        // Versions less than 2 didn't have an entry size, it was hard coded
                        m_kext_summary_header.entry_size = KERNEL_MODULE_ENTRY_SIZE_VERSION_1;
                    }
                    m_kext_summary_header.entry_count = data.GetU32(&offset);
                    return true;
                }
            }
        }
    }
    m_kext_summary_header_addr.Clear();
    return false;
}

// We've either (a) just attached to a new kernel, or (b) the kexts-changed breakpoint was hit
// and we need to figure out what kexts have been added or removed.
// Read the kext summaries from the inferior kernel memory, compare them against the
// m_known_kexts vector and update the m_known_kexts vector as needed to keep in sync with the
// inferior.

bool
DynamicLoaderDarwinKernel::ParseKextSummaries (const Address &kext_summary_addr, uint32_t count)
{
    KextImageInfo::collection kext_summaries;
    Log *log(GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    if (log)
        log->Printf ("Kexts-changed breakpoint hit, there are %d kexts currently.\n", count);
        
    Mutex::Locker locker(m_mutex);

    if (!ReadKextSummaries (kext_summary_addr, count, kext_summaries))
        return false;

    // read the plugin.dynamic-loader.darwin-kernel.load-kexts setting -- if the user requested no 
    // kext loading, don't print any messages about kexts & don't try to read them.
    const bool load_kexts = GetGlobalProperties()->GetLoadKexts();

    // By default, all kexts we've loaded in the past are marked as "remove" and all of the kexts
    // we just found out about from ReadKextSummaries are marked as "add".
    std::vector<bool> to_be_removed(m_known_kexts.size(), true);
    std::vector<bool> to_be_added(count, true);

    int number_of_new_kexts_being_added = 0;
    int number_of_old_kexts_being_removed = m_known_kexts.size();

    const uint32_t new_kexts_size = kext_summaries.size();
    const uint32_t old_kexts_size = m_known_kexts.size();

    // The m_known_kexts vector may have entries that have been Cleared,
    // or are a kernel.  
    for (uint32_t old_kext = 0; old_kext < old_kexts_size; old_kext++)
    {
        bool ignore = false;
        KextImageInfo &image_info = m_known_kexts[old_kext];
        if (image_info.IsKernel())
        {
            ignore = true;
        }
        else if (image_info.GetLoadAddress() == LLDB_INVALID_ADDRESS && !image_info.GetModule())
        {
            ignore = true;
        }

        if (ignore)
        {
            number_of_old_kexts_being_removed--;
            to_be_removed[old_kext] = false;
        }
    }

    // Scan over the list of kexts we just read from the kernel, note those that
    // need to be added and those already loaded.
    for (uint32_t new_kext = 0; new_kext < new_kexts_size; new_kext++)
    {
        bool add_this_one = true;
        for (uint32_t old_kext = 0; old_kext < old_kexts_size; old_kext++)
        {
            if (m_known_kexts[old_kext] == kext_summaries[new_kext])
            {
                // We already have this kext, don't re-load it.
                to_be_added[new_kext] = false;
                // This kext is still present, do not remove it.
                to_be_removed[old_kext] = false;

                number_of_old_kexts_being_removed--;
                add_this_one = false;
                break;
            }
        }
        if (add_this_one)
        {
            number_of_new_kexts_being_added++;
        }
    }

    if (number_of_new_kexts_being_added == 0 && number_of_old_kexts_being_removed == 0)
        return true;

    Stream *s = &m_process->GetTarget().GetDebugger().GetOutputStream();
    if (s && load_kexts)
    {
        if (number_of_new_kexts_being_added > 0 && number_of_old_kexts_being_removed > 0)
        {
            s->Printf ("Loading %d kext modules and unloading %d kext modules ", number_of_new_kexts_being_added, number_of_old_kexts_being_removed);
        }
        else if (number_of_new_kexts_being_added > 0)
        {
            s->Printf ("Loading %d kext modules ", number_of_new_kexts_being_added);
        }
        else if (number_of_old_kexts_being_removed > 0)
        {
            s->Printf ("Unloading %d kext modules ", number_of_old_kexts_being_removed);
        }
    }

    if (log)
    {
        if (load_kexts)
        {
            log->Printf ("DynamicLoaderDarwinKernel::ParseKextSummaries: %d kexts added, %d kexts removed", number_of_new_kexts_being_added, number_of_old_kexts_being_removed);
        }
        else
        {
            log->Printf ("DynamicLoaderDarwinKernel::ParseKextSummaries kext loading is disabled, else would have %d kexts added, %d kexts removed", number_of_new_kexts_being_added, number_of_old_kexts_being_removed);
        }
    }


    if (number_of_new_kexts_being_added > 0)
    {
        ModuleList loaded_module_list;

        const uint32_t num_of_new_kexts = kext_summaries.size();
        for (uint32_t new_kext = 0; new_kext < num_of_new_kexts; new_kext++)
        {
            if (to_be_added[new_kext] == true)
            {
                KextImageInfo &image_info = kext_summaries[new_kext];
                if (load_kexts)
                {
                    if (!image_info.LoadImageUsingMemoryModule (m_process))
                    {
                        image_info.LoadImageAtFileAddress (m_process);
                    }
                }

                m_known_kexts.push_back(image_info);

                if (image_info.GetModule() && m_process->GetStopID() == image_info.GetProcessStopId())
                    loaded_module_list.AppendIfNeeded (image_info.GetModule());

                if (s && load_kexts)
                    s->Printf (".");

                if (log)
                    kext_summaries[new_kext].PutToLog (log);
            }
        }
        m_process->GetTarget().ModulesDidLoad (loaded_module_list);
    }

    if (number_of_old_kexts_being_removed > 0)
    {
        ModuleList loaded_module_list;
        const uint32_t num_of_old_kexts = m_known_kexts.size();
        for (uint32_t old_kext = 0; old_kext < num_of_old_kexts; old_kext++)
        {
            ModuleList unloaded_module_list;
            if (to_be_removed[old_kext])
            {
                KextImageInfo &image_info = m_known_kexts[old_kext];
                // You can't unload the kernel.
                if (!image_info.IsKernel())
                {
                    if (image_info.GetModule())
                    {
                        unloaded_module_list.AppendIfNeeded (image_info.GetModule());
                    }
                    if (s)
                        s->Printf (".");
                    image_info.Clear();
                    // should pull it out of the KextImageInfos vector but that would mutate the list and invalidate
                    // the to_be_removed bool vector; leaving it in place once Cleared() is relatively harmless.
                }
            }
            m_process->GetTarget().ModulesDidUnload (unloaded_module_list);
        }
    }

    if (s && load_kexts)
    {
        s->Printf (" done.\n");
        s->Flush ();
    }

    return true;
}

uint32_t
DynamicLoaderDarwinKernel::ReadKextSummaries (const Address &kext_summary_addr,
                                              uint32_t image_infos_count, 
                                              KextImageInfo::collection &image_infos)
{
    const ByteOrder endian = m_kernel.GetByteOrder();
    const uint32_t addr_size = m_kernel.GetAddressByteSize();

    image_infos.resize(image_infos_count);
    const size_t count = image_infos.size() * m_kext_summary_header.entry_size;
    DataBufferHeap data(count, 0);
    Error error;
    
    const bool prefer_file_cache = false;
    const size_t bytes_read = m_process->GetTarget().ReadMemory (kext_summary_addr, 
                                                                 prefer_file_cache,
                                                                 data.GetBytes(), 
                                                                 data.GetByteSize(),
                                                                 error);
    if (bytes_read == count)
    {
        
        DataExtractor extractor (data.GetBytes(), data.GetByteSize(), endian, addr_size);
        uint32_t i=0;
        for (uint32_t kext_summary_offset = 0;
             i < image_infos.size() && extractor.ValidOffsetForDataOfSize(kext_summary_offset, m_kext_summary_header.entry_size); 
             ++i, kext_summary_offset += m_kext_summary_header.entry_size)
        {
            lldb::offset_t offset = kext_summary_offset;
            const void *name_data = extractor.GetData(&offset, KERNEL_MODULE_MAX_NAME);
            if (name_data == NULL)
                break;
            image_infos[i].SetName ((const char *) name_data);
            UUID uuid (extractor.GetData (&offset, 16), 16);
            image_infos[i].SetUUID (uuid);
            image_infos[i].SetLoadAddress (extractor.GetU64(&offset));
            image_infos[i].SetSize (extractor.GetU64(&offset));
        }
        if (i < image_infos.size())
            image_infos.resize(i);
    }
    else
    {
        image_infos.clear();
    }
    return image_infos.size();
}

bool
DynamicLoaderDarwinKernel::ReadAllKextSummaries ()
{
    Mutex::Locker locker(m_mutex);
    
    if (ReadKextSummaryHeader ())
    {
        if (m_kext_summary_header.entry_count > 0 && m_kext_summary_header_addr.IsValid())
        {
            Address summary_addr (m_kext_summary_header_addr);
            summary_addr.Slide(m_kext_summary_header.GetSize());
            if (!ParseKextSummaries (summary_addr, m_kext_summary_header.entry_count))
            {
                m_known_kexts.clear();
            }
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------
// Dump an image info structure to the file handle provided.
//----------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::KextImageInfo::PutToLog (Log *log) const
{
    if (log == NULL)
        return;
    const uint8_t *u = (uint8_t *) m_uuid.GetBytes();

    if (m_load_address == LLDB_INVALID_ADDRESS)
    {
        if (u)
        {
            log->Printf("\tuuid=%2.2X%2.2X%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X name=\"%s\" (UNLOADED)",
                        u[ 0], u[ 1], u[ 2], u[ 3],
                        u[ 4], u[ 5], u[ 6], u[ 7],
                        u[ 8], u[ 9], u[10], u[11],
                        u[12], u[13], u[14], u[15],
                        m_name.c_str());
        }
        else
            log->Printf("\tname=\"%s\" (UNLOADED)", m_name.c_str());
    }
    else
    {
        if (u)
        {
            log->Printf("\taddr=0x%16.16" PRIx64 " size=0x%16.16" PRIx64 " uuid=%2.2X%2.2X%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X name=\"%s\"",
                        m_load_address, m_size, 
                        u[ 0], u[ 1], u[ 2], u[ 3], u[ 4], u[ 5], u[ 6], u[ 7],
                        u[ 8], u[ 9], u[10], u[11], u[12], u[13], u[14], u[15],
                        m_name.c_str());
        }
        else
        {
            log->Printf("\t[0x%16.16" PRIx64 " - 0x%16.16" PRIx64 ") name=\"%s\"",
                        m_load_address, m_load_address+m_size, m_name.c_str());
        }
    }
}

//----------------------------------------------------------------------
// Dump the _dyld_all_image_infos members and all current image infos
// that we have parsed to the file handle provided.
//----------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::PutToLog(Log *log) const
{
    if (log == NULL)
        return;

    Mutex::Locker locker(m_mutex);
    log->Printf("gLoadedKextSummaries = 0x%16.16" PRIx64 " { version=%u, entry_size=%u, entry_count=%u }",
                m_kext_summary_header_addr.GetFileAddress(),
                m_kext_summary_header.version,
                m_kext_summary_header.entry_size,
                m_kext_summary_header.entry_count);

    size_t i;
    const size_t count = m_known_kexts.size();
    if (count > 0)
    {
        log->PutCString("Loaded:");
        for (i = 0; i<count; i++)
            m_known_kexts[i].PutToLog(log);
    }
}

void
DynamicLoaderDarwinKernel::PrivateInitialize(Process *process)
{
    DEBUG_PRINTF("DynamicLoaderDarwinKernel::%s() process state = %s\n", __FUNCTION__, StateAsCString(m_process->GetState()));
    Clear(true);
    m_process = process;
}

void
DynamicLoaderDarwinKernel::SetNotificationBreakpointIfNeeded ()
{
    if (m_break_id == LLDB_INVALID_BREAK_ID && m_kernel.GetModule())
    {
        DEBUG_PRINTF("DynamicLoaderDarwinKernel::%s() process state = %s\n", __FUNCTION__, StateAsCString(m_process->GetState()));

        
        const bool internal_bp = true;
        const LazyBool skip_prologue = eLazyBoolNo;
        FileSpecList module_spec_list;
        module_spec_list.Append (m_kernel.GetModule()->GetFileSpec());
        Breakpoint *bp = m_process->GetTarget().CreateBreakpoint (&module_spec_list,
                                                                  NULL,
                                                                  "OSKextLoadedKextSummariesUpdated",
                                                                  eFunctionNameTypeFull,
                                                                  skip_prologue,
                                                                  internal_bp).get();

        bp->SetCallback (DynamicLoaderDarwinKernel::BreakpointHitCallback, this, true);
        m_break_id = bp->GetID();
    }
}

//----------------------------------------------------------------------
// Member function that gets called when the process state changes.
//----------------------------------------------------------------------
void
DynamicLoaderDarwinKernel::PrivateProcessStateChanged (Process *process, StateType state)
{
    DEBUG_PRINTF("DynamicLoaderDarwinKernel::%s(%s)\n", __FUNCTION__, StateAsCString(state));
    switch (state)
    {
    case eStateConnected:
    case eStateAttaching:
    case eStateLaunching:
    case eStateInvalid:
    case eStateUnloaded:
    case eStateExited:
    case eStateDetached:
        Clear(false);
        break;

    case eStateStopped:
        UpdateIfNeeded();
        break;

    case eStateRunning:
    case eStateStepping:
    case eStateCrashed:
    case eStateSuspended:
        break;
    }
}

ThreadPlanSP
DynamicLoaderDarwinKernel::GetStepThroughTrampolinePlan (Thread &thread, bool stop_others)
{
    ThreadPlanSP thread_plan_sp;
    Log *log(GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (log)
        log->Printf ("Could not find symbol for step through.");
    return thread_plan_sp;
}

Error
DynamicLoaderDarwinKernel::CanLoadImage ()
{
    Error error;
    error.SetErrorString("always unsafe to load or unload shared libraries in the darwin kernel");
    return error;
}

void
DynamicLoaderDarwinKernel::Initialize()
{
    PluginManager::RegisterPlugin (GetPluginNameStatic(),
                                   GetPluginDescriptionStatic(),
                                   CreateInstance,
                                   DebuggerInitialize);
}

void
DynamicLoaderDarwinKernel::Terminate()
{
    PluginManager::UnregisterPlugin (CreateInstance);
}

void
DynamicLoaderDarwinKernel::DebuggerInitialize (lldb_private::Debugger &debugger)
{
    if (!PluginManager::GetSettingForDynamicLoaderPlugin (debugger, DynamicLoaderDarwinKernelProperties::GetSettingName()))
    {
        const bool is_global_setting = true;
        PluginManager::CreateSettingForDynamicLoaderPlugin (debugger,
                                                            GetGlobalProperties()->GetValueProperties(),
                                                            ConstString ("Properties for the DynamicLoaderDarwinKernel plug-in."),
                                                            is_global_setting);
    }
}

const char *
DynamicLoaderDarwinKernel::GetPluginNameStatic()
{
    return "dynamic-loader.darwin-kernel";
}

const char *
DynamicLoaderDarwinKernel::GetPluginDescriptionStatic()
{
    return "Dynamic loader plug-in that watches for shared library loads/unloads in the MacOSX kernel.";
}


//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
const char *
DynamicLoaderDarwinKernel::GetPluginName()
{
    return "DynamicLoaderDarwinKernel";
}

const char *
DynamicLoaderDarwinKernel::GetShortPluginName()
{
    return GetPluginNameStatic();
}

uint32_t
DynamicLoaderDarwinKernel::GetPluginVersion()
{
    return 1;
}

lldb::ByteOrder
DynamicLoaderDarwinKernel::GetByteOrderFromMagic (uint32_t magic)
{
    switch (magic)
    {
        case llvm::MachO::HeaderMagic32:
        case llvm::MachO::HeaderMagic64:
            return lldb::endian::InlHostByteOrder();
            
        case llvm::MachO::HeaderMagic32Swapped:
        case llvm::MachO::HeaderMagic64Swapped:
            if (lldb::endian::InlHostByteOrder() == lldb::eByteOrderBig)
                return lldb::eByteOrderLittle;
            else
                return lldb::eByteOrderBig;
            
        default:
            break;
    }
    return lldb::eByteOrderInvalid;
}

