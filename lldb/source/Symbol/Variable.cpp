//===-- Variable.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Variable.h"

#include "lldb/Core/Stream.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// Variable constructor
//----------------------------------------------------------------------
Variable::Variable 
(
    lldb::user_id_t uid,
    const char *name, 
    const char *mangled,   // The mangled variable name for variables in namespaces
    const lldb::SymbolFileTypeSP &symfile_type_sp,
    ValueType scope,
    SymbolContextScope *context,
    Declaration* decl_ptr,
    const DWARFExpression& location,
    bool external,
    bool artificial
) :
    UserID(uid),
    m_name(name),
    m_mangled (mangled, true),
    m_symfile_type_sp(symfile_type_sp),
    m_scope(scope),
    m_owner_scope(context),
    m_declaration(decl_ptr),
    m_location(location),
    m_external(external),
    m_artificial(artificial)
{
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
Variable::~Variable()
{
}


const ConstString&
Variable::GetName() const
{
    if (m_mangled)
        return m_mangled.GetName();
    return m_name;
}

bool
Variable::NameMatches (const RegularExpression& regex) const
{
    if (regex.Execute (m_name.AsCString()))
        return true;
    return m_mangled.NameMatches (regex);
}

Type *
Variable::GetType()
{
    if (m_symfile_type_sp)
        return m_symfile_type_sp->GetType();
    return NULL;
}

void
Variable::Dump(Stream *s, bool show_context) const
{
    s->Printf("%p: ", this);
    s->Indent();
    *s << "Variable" << (const UserID&)*this;

    if (m_name)
        *s << ", name = \"" << m_name << "\"";

    if (m_symfile_type_sp)
    {
        Type *type = m_symfile_type_sp->GetType();
        if (type)
        {
            *s << ", type = {" << type->GetID() << "} " << (void*)type << " (";
            type->DumpTypeName(s);
            s->PutChar(')');
        }
    }

    if (m_scope != eValueTypeInvalid)
    {
        s->PutCString(", scope = ");
        switch (m_scope)
        {
        case eValueTypeVariableGlobal:       s->PutCString(m_external ? "global" : "static"); break;
        case eValueTypeVariableArgument:    s->PutCString("parameter"); break;
        case eValueTypeVariableLocal:        s->PutCString("local"); break;
        default:            *s << "??? (" << m_scope << ')';
        }
    }

    if (show_context && m_owner_scope != NULL)
    {
        s->PutCString(", context = ( ");
        m_owner_scope->DumpSymbolContext(s);
        s->PutCString(" )");
    }

    bool show_fullpaths = false;
    m_declaration.Dump(s, show_fullpaths);

    if (m_location.IsValid())
    {
        s->PutCString(", location = ");
        lldb::addr_t loclist_base_addr = LLDB_INVALID_ADDRESS;
        if (m_location.IsLocationList())
        {
            SymbolContext variable_sc;
            m_owner_scope->CalculateSymbolContext(&variable_sc);
            if (variable_sc.function)
                loclist_base_addr = variable_sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
        }
        ABI *abi = NULL;
        if (m_owner_scope)
        {
            ModuleSP module_sp (m_owner_scope->CalculateSymbolContextModule());
            if (module_sp)
                abi = ABI::FindPlugin (module_sp->GetArchitecture()).get();
        }
        m_location.GetDescription(s, lldb::eDescriptionLevelBrief, loclist_base_addr, abi);
    }

    if (m_external)
        s->PutCString(", external");

    if (m_artificial)
        s->PutCString(", artificial");

    s->EOL();
}

bool
Variable::DumpDeclaration (Stream *s, bool show_fullpaths, bool show_module)
{
    bool dumped_declaration_info = false;
    if (m_owner_scope)
    {
        SymbolContext sc;
        m_owner_scope->CalculateSymbolContext(&sc);
        sc.block = NULL;
        sc.line_entry.Clear();
        bool show_inlined_frames = false;
    
        dumped_declaration_info = sc.DumpStopContext (s, 
                                                      NULL, 
                                                      Address(), 
                                                      show_fullpaths, 
                                                      show_module, 
                                                      show_inlined_frames);
        
        if (sc.function)
            s->PutChar(':');
    }
    if (m_declaration.DumpStopContext (s, false))
        dumped_declaration_info = true;
    return dumped_declaration_info;
}

size_t
Variable::MemorySize() const
{
    return sizeof(Variable);
}


void
Variable::CalculateSymbolContext (SymbolContext *sc)
{
    if (m_owner_scope)
        m_owner_scope->CalculateSymbolContext(sc);
    else
        sc->Clear();
}

bool
Variable::LocationIsValidForFrame (StackFrame *frame)
{
    // Is the variable is described by a single location?
    if (!m_location.IsLocationList())
    {
        // Yes it is, the location is valid. 
        return true;
    }

    if (frame)
    {
        Function *function = frame->GetSymbolContext(eSymbolContextFunction).function;
        if (function)
        {
            TargetSP target_sp (frame->CalculateTarget());
            
            addr_t loclist_base_load_addr = function->GetAddressRange().GetBaseAddress().GetLoadAddress (target_sp.get());
            if (loclist_base_load_addr == LLDB_INVALID_ADDRESS)
                return false;
            // It is a location list. We just need to tell if the location
            // list contains the current address when converted to a load
            // address
            return m_location.LocationListContainsAddress (loclist_base_load_addr, 
                                                           frame->GetFrameCodeAddress().GetLoadAddress (target_sp.get()));
        }
    }
    return false;
}

bool
Variable::LocationIsValidForAddress (const Address &address)
{
    // Be sure to resolve the address to section offset prior to 
    // calling this function.
    if (address.IsSectionOffset())
    {
        SymbolContext sc;
        CalculateSymbolContext(&sc);
        if (sc.module_sp == address.GetModule())
        {
            // Is the variable is described by a single location?
            if (!m_location.IsLocationList())
            {
                // Yes it is, the location is valid. 
                return true;
            }
            
            if (sc.function)
            {
                addr_t loclist_base_file_addr = sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
                if (loclist_base_file_addr == LLDB_INVALID_ADDRESS)
                    return false;
                // It is a location list. We just need to tell if the location
                // list contains the current address when converted to a load
                // address
                return m_location.LocationListContainsAddress (loclist_base_file_addr, 
                                                               address.GetFileAddress());
            }
        }
    }
    return false;
}

bool
Variable::IsInScope (StackFrame *frame)
{
    switch (m_scope)
    {
    case eValueTypeRegister:
    case eValueTypeRegisterSet:
        return frame != NULL;

    case eValueTypeConstResult:
    case eValueTypeVariableGlobal:
    case eValueTypeVariableStatic:
        return true;

    case eValueTypeVariableArgument:
    case eValueTypeVariableLocal:
        if (frame)
        {
            // We don't have a location list, we just need to see if the block
            // that this variable was defined in is currently
            Block *deepest_frame_block = frame->GetSymbolContext(eSymbolContextBlock).block;
            if (deepest_frame_block)
            {
                SymbolContext variable_sc;
                CalculateSymbolContext (&variable_sc);
                // Check for static or global variable defined at the compile unit 
                // level that wasn't defined in a block
                if (variable_sc.block == NULL)
                    return true;    

                if (variable_sc.block == deepest_frame_block)
                    return true;
                return variable_sc.block->Contains (deepest_frame_block);
            }
        }
        break;

    default:
        break;
    }
    return false;
}

Error
Variable::GetValuesForVariableExpressionPath (const char *variable_expr_path,
                                              ExecutionContextScope *scope,
                                              GetVariableCallback callback,
                                              void *baton,
                                              VariableList &variable_list,
                                              ValueObjectList &valobj_list)
{
    Error error;
    if (variable_expr_path && callback)
    {
        switch (variable_expr_path[0])
        {
        case '*':
            {
                error = Variable::GetValuesForVariableExpressionPath (variable_expr_path + 1,
                                                                      scope,
                                                                      callback,
                                                                      baton,
                                                                      variable_list,
                                                                      valobj_list);
                if (error.Success())
                {
                    for (uint32_t i=0; i<valobj_list.GetSize(); )
                    {
                        Error tmp_error;
                        ValueObjectSP valobj_sp (valobj_list.GetValueObjectAtIndex(i)->Dereference(tmp_error));
                        if (tmp_error.Fail())
                        {
                            variable_list.RemoveVariableAtIndex (i);
                            valobj_list.RemoveValueObjectAtIndex (i);
                        }
                        else
                        {
                            valobj_list.SetValueObjectAtIndex (i, valobj_sp);
                            ++i;
                        }
                    }
                }
                else
                {
                    error.SetErrorString ("unknown error");
                }
                return error;
            }
            break;
        
        case '&':
            {
                error = Variable::GetValuesForVariableExpressionPath (variable_expr_path + 1,
                                                                      scope,
                                                                      callback,
                                                                      baton,
                                                                      variable_list,
                                                                      valobj_list);
                if (error.Success())
                {
                    for (uint32_t i=0; i<valobj_list.GetSize(); )
                    {
                        Error tmp_error;
                        ValueObjectSP valobj_sp (valobj_list.GetValueObjectAtIndex(i)->AddressOf(tmp_error));
                        if (tmp_error.Fail())
                        {
                            variable_list.RemoveVariableAtIndex (i);
                            valobj_list.RemoveValueObjectAtIndex (i);
                        }
                        else
                        {
                            valobj_list.SetValueObjectAtIndex (i, valobj_sp);
                            ++i;
                        }
                    }
                }
                else
                {
                    error.SetErrorString ("unknown error");
                }
                return error;
            }
            break;
            
        default:
            {
                RegularExpression regex ("^([A-Za-z_:][A-Za-z_0-9:]*)(.*)");
                if (regex.Execute(variable_expr_path, 1))
                {
                    std::string variable_name;
                    if (regex.GetMatchAtIndex(variable_expr_path, 1, variable_name))
                    {
                        variable_list.Clear();
                        if (callback (baton, variable_name.c_str(), variable_list))
                        {
                            uint32_t i=0;
                            while (i < variable_list.GetSize())
                            {
                                VariableSP var_sp (variable_list.GetVariableAtIndex (i));
                                ValueObjectSP valobj_sp;
                                if (var_sp)
                                {
                                    ValueObjectSP variable_valobj_sp(ValueObjectVariable::Create (scope, var_sp));
                                    if (variable_valobj_sp)
                                    {
                                        variable_expr_path += variable_name.size();
                                        if (*variable_expr_path)
                                        {
                                            const char* first_unparsed = NULL;
                                            ValueObject::ExpressionPathScanEndReason reason_to_stop;
                                            ValueObject::ExpressionPathEndResultType final_value_type;
                                            ValueObject::GetValueForExpressionPathOptions options;
                                            ValueObject::ExpressionPathAftermath final_task_on_target;

                                            valobj_sp = variable_valobj_sp->GetValueForExpressionPath (variable_expr_path,
                                                                                                       &first_unparsed,
                                                                                                       &reason_to_stop,
                                                                                                       &final_value_type,
                                                                                                       options,
                                                                                                       &final_task_on_target);
                                            if (!valobj_sp)
                                            {
                                                error.SetErrorStringWithFormat ("invalid expression path '%s' for variable '%s'",
                                                                                variable_expr_path,
                                                                                var_sp->GetName().GetCString());
                                            }
                                        }
                                        else
                                        {
                                            // Just the name of a variable with no extras
                                            valobj_sp = variable_valobj_sp;
                                        }
                                    }
                                }

                                if (!var_sp || !valobj_sp)
                                {
                                    variable_list.RemoveVariableAtIndex (i);
                                }
                                else
                                {
                                    valobj_list.Append(valobj_sp);
                                    ++i;
                                }
                            }
                            
                            if (variable_list.GetSize() > 0)
                            {
                                error.Clear();
                                return error;
                            }
                        }
                    }
                }
                error.SetErrorStringWithFormat ("unable to extracta variable name from '%s'", variable_expr_path);
            }
            break;
        }
    }
    error.SetErrorString ("unknown error");
    return error;
}

bool
Variable::DumpLocationForAddress (Stream *s, const Address &address)
{
    // Be sure to resolve the address to section offset prior to 
    // calling this function.
    if (address.IsSectionOffset())
    {
        SymbolContext sc;
        CalculateSymbolContext(&sc);
        if (sc.module_sp == address.GetModule())
        {
            ABI *abi = NULL;
            if (m_owner_scope)
            {
                ModuleSP module_sp (m_owner_scope->CalculateSymbolContextModule());
                if (module_sp)
                    abi = ABI::FindPlugin (module_sp->GetArchitecture()).get();
            }

            const addr_t file_addr = address.GetFileAddress();
            if (sc.function)
            {
                if (sc.function->GetAddressRange().ContainsFileAddress(address))
                {
                    addr_t loclist_base_file_addr = sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
                    if (loclist_base_file_addr == LLDB_INVALID_ADDRESS)
                        return false;
                    return m_location.DumpLocationForAddress (s, 
                                                              eDescriptionLevelBrief, 
                                                              loclist_base_file_addr, 
                                                              file_addr,
                                                              abi);
                }
            }
            return m_location.DumpLocationForAddress (s, 
                                                      eDescriptionLevelBrief, 
                                                      LLDB_INVALID_ADDRESS, 
                                                      file_addr,
                                                      abi);
        }
    }
    return false;

}

