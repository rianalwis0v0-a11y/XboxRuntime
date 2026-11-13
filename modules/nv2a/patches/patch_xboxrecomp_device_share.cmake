# Share the host-owned D3D8 device with the NV2A module's statically linked
# copy of xbox_d3d8. Each DLL has its own static-library globals otherwise.
set(_d3d_dir "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/xboxrecomp/src/d3d")
set(_d3d_header "${_d3d_dir}/d3d8_xbox.h")
set(_d3d_source "${_d3d_dir}/d3d8_device.c")
if(NOT EXISTS "${_d3d_header}" OR NOT EXISTS "${_d3d_source}")
    message(FATAL_ERROR "Pinned xboxrecomp D3D8 device sources are missing")
endif()

file(READ "${_d3d_header}" _header)
if(NOT _header MATCHES "xbox_SetD3DDevice")
    string(REPLACE
        [=[IDirect3DDevice8 *xbox_GetD3DDevice(void);]=]
        [=[IDirect3DDevice8 *xbox_GetD3DDevice(void);
void xbox_SetD3DDevice(IDirect3DDevice8 *device);]=]
        _header "${_header}")
    if(NOT _header MATCHES "xbox_SetD3DDevice")
        message(FATAL_ERROR "Could not add the D3D8 external-device setter declaration")
    endif()
    file(WRITE "${_d3d_header}" "${_header}")
endif()

file(READ "${_d3d_source}" _source)
if(NOT _source MATCHES "xbox_SetD3DDevice")
    string(REPLACE
        [=[static BOOL g_device_initialized = FALSE;]=]
        [=[static BOOL g_device_initialized = FALSE;
static IDirect3DDevice8 *g_external_device = NULL;]=]
        _source "${_source}")

    set(_old_get [=[
IDirect3DDevice8 *xbox_GetD3DDevice(void)
{
    return g_device_initialized ? &g_device : NULL;
}
]=])
    set(_new_get [=[
IDirect3DDevice8 *xbox_GetD3DDevice(void)
{
    if (g_external_device)
        return g_external_device;
    return g_device_initialized ? &g_device : NULL;
}

void xbox_SetD3DDevice(IDirect3DDevice8 *device)
{
    if (g_external_device == device)
        return;
    if (g_external_device && g_external_device->lpVtbl &&
        g_external_device->lpVtbl->Release)
        g_external_device->lpVtbl->Release(g_external_device);
    g_external_device = device;
    if (g_external_device && g_external_device->lpVtbl &&
        g_external_device->lpVtbl->AddRef)
        g_external_device->lpVtbl->AddRef(g_external_device);
}
]=])
    string(REPLACE "${_old_get}" "${_new_get}" _source "${_source}")
    if(NOT _source MATCHES "void xbox_SetD3DDevice")
        message(FATAL_ERROR "Could not patch xbox_GetD3DDevice with external-device sharing")
    endif()
    file(WRITE "${_d3d_source}" "${_source}")
endif()
