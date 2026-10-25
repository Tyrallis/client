#include "WarcraftXL/Binding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/SIMD.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Mopaq/MPQGlobals.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Mopaq/SFileArchives.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/SoundEngine/fmodex/src/fmod.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Tempest/CRect.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/Container.h"
#include "WoW/Source/Object/Corpse.h"
#include "WoW/Source/Object/DynamicObject.h"
#include "WoW/Source/Object/GameObject.h"
#include "WoW/Source/Object/Item.h"
#include "WoW/Source/Object/Object.h"
#include "WoW/Source/Object/ObjectClient/Container_C.h"
#include "WoW/Source/Object/ObjectClient/Corpse_C.h"
#include "WoW/Source/Object/ObjectClient/DynamicObject_C.h"
#include "WoW/Source/Object/ObjectClient/GameObject_C.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Types.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/ObjectClient/VisibleItem_C.h"
#include "WoW/Source/Object/Player.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/WowServices/Login.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/MovementShared.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/ChatFrame.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/MapDoodadDef.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/World.h"
#include "wxl/core/gpu.hpp"
#include "wxl/core/storm.hpp"
#include "wxl/events.h"
#include "wxl/objects/fields.hpp"
#include "wxl/services/script.h"

// The SDK's update-field table describes these blocks: each kind's fields end where the client's
// descriptor block for it does, and a player other than the active one keeps CGPlayerData only.
// core/tools/fields/generate_update_fields.py writes the table from this fork and checks it
// whole; these hold its block sizes to the structs this build compiles.
#define WXL_UPDATE_FIELD(kind, NAME, Name, index, size, value, flags, watch)
#define WXL_UPDATE_FIELD_KIND(kind, type, parent, first, end, others_end) \
    constexpr uint32_t kFieldsOf##kind[3] = { first, end, others_end };
#include "wxl/update_fields.def"
#undef WXL_UPDATE_FIELD_KIND
#undef WXL_UPDATE_FIELD
static_assert(sizeof(CGObjectData) == (kFieldsOfObject[1] - kFieldsOfObject[0]) * 4, "CGObjectData");
static_assert(sizeof(CGItemData) == (kFieldsOfItem[1] - kFieldsOfItem[0]) * 4, "CGItemData");
static_assert(sizeof(CGContainerData) == (kFieldsOfContainer[1] - kFieldsOfContainer[0]) * 4, "CGContainerData");
static_assert(sizeof(CGUnitData) == (kFieldsOfUnit[1] - kFieldsOfUnit[0]) * 4, "CGUnitData");
static_assert(sizeof(CGPlayerLocalData) == (kFieldsOfPlayer[1] - kFieldsOfPlayer[0]) * 4, "CGPlayerLocalData");
static_assert(sizeof(CGPlayerData) == (kFieldsOfPlayer[2] - kFieldsOfPlayer[0]) * 4, "CGPlayerData");
static_assert(sizeof(CGGameObjectData) == (kFieldsOfGameObject[1] - kFieldsOfGameObject[0]) * 4, "CGGameObjectData");
static_assert(sizeof(CGDynamicObjectData) == (kFieldsOfDynamicObject[1] - kFieldsOfDynamicObject[0]) * 4,
              "CGDynamicObjectData");
static_assert(sizeof(CGCorpseData) == (kFieldsOfCorpse[1] - kFieldsOfCorpse[0]) * 4, "CGCorpseData");
static_assert(offsetof(CGUnitData, m_unitFieldHealth) == (wxl::field::Unit::Health.index - kFieldsOfUnit[0]) * 4 &&
                  offsetof(CGPlayerData, m_playerChosenTitle) ==
                      (wxl::field::Player::ChosenTitle.index - kFieldsOfPlayer[0]) * 4,
              "UNIT_FIELD_HEALTH and PLAYER_CHOSEN_TITLE where update_fields.def puts them");

// The contract numbers object types exactly as the client does, so a type id crosses as is.
static_assert(ID_OBJECT == WXL_OBJECT_UNKNOWN && ID_ITEM == WXL_OBJECT_ITEM && ID_CONTAINER == WXL_OBJECT_CONTAINER
                  && ID_UNIT == WXL_OBJECT_UNIT && ID_PLAYER == WXL_OBJECT_PLAYER && ID_GAMEOBJECT == WXL_OBJECT_GAMEOBJECT
                  && ID_DYNAMICOBJECT == WXL_OBJECT_DYNAMIC && ID_CORPSE == WXL_OBJECT_CORPSE,
              "wxl_object_type numbers the client's OBJECT_TYPE_ID");

namespace {
    // The runtime hands back the CGObject_C* a seam gave it, and asks a kind's rows only for an
    // object of that kind, so each cast below is the one the client itself would make.
    const CGObject_C* Object(const void* object) {
        return static_cast<const CGObject_C*>(object);
    }

    const CGCorpse_C* Corpse(const void* object) {
        return static_cast<const CGCorpse_C*>(Object(object));
    }

    template <class T, size_t N>
    constexpr uint32_t Extent(const T (&)[N]) {
        return static_cast<uint32_t>(N);
    }

    wxl_bool Give(wxl_vec3* out, const C3Vector& v) {
        out->x = v.x;
        out->y = v.y;
        out->z = v.z;
        return WXL_TRUE;
    }

    lua_State* State(void* state) {
        return static_cast<lua_State*>(state);
    }

    // The runtime's mirror handler, called from the client's own MIRRORHANDLERFUNC: the guid
    // crosses as its 64 bits. The runtime sets one handler for every field it watches.
    wxl_core_mirror_fn s_mirror = nullptr;

    int32_t ForwardMirror(WOWGUID guid, uint32_t offset, uint32_t size, const void* oldValue, void* param) {
        return s_mirror ? s_mirror(guid.m_guid, offset, size, oldValue, param) : 0;
    }

    bool TakeMirror(wxl_core_mirror_fn handler) {
        if (!handler || (s_mirror && s_mirror != handler)) {
            return false;
        }
        s_mirror = handler;
        return true;
    }

    // A member as a binding row answers it: a guid as its 64 bits, a byte array as the integer it
    // packs, anything else converted to the row's type.
    template <class R, class V>
    R Fact(const V& value) {
        if constexpr (std::is_same_v<V, WOWGUID>) {
            return value.m_guid;
        } else if constexpr (std::is_array_v<V>) {
            static_assert(sizeof(V) == sizeof(R), "a byte array answers an integer of its size");
            R packed;
            std::memcpy(&packed, &value, sizeof packed);
            return packed;
        } else {
            return static_cast<R>(value);
        }
    }
}

namespace WarcraftXL::Binding {
    // --- the field rows, generated from the table --------------------------------------------

#define WXL_BINDING(name, ret, params, args, absent)

#define WXL_BINDING_FIELD(name, ret, Type, member, absent)                                              \
    ret WXL_CALL name(const void* object) {                                                             \
        return object ? Fact<ret>(static_cast<const Type*>(object)->member) : static_cast<ret>(absent); \
    }

#define WXL_BINDING_FIELD_VIA(name, ret, Type, via, ViaType, member, absent)             \
    ret WXL_CALL name(const void* object) {                                              \
        const ViaType* block = object ? static_cast<const Type*>(object)->via : nullptr; \
        return block ? Fact<ret>(block->member) : static_cast<ret>(absent);              \
    }

#define WXL_BINDING_FIELD_VIA_INDEX(name, ret, Type, via, ViaType, member)               \
    wxl_bool WXL_CALL name(const void* object, uint32_t index, ret* out) {               \
        const ViaType* block = object ? static_cast<const Type*>(object)->via : nullptr; \
        if (!block || !out || index >= Extent(block->member)) {                          \
            return WXL_FALSE;                                                            \
        }                                                                                \
        *out = Fact<ret>(block->member[index]);                                          \
        return WXL_TRUE;                                                                 \
    }

#define WXL_BINDING_FIELD_VIA_AT(name, ret, Type, via, ViaType, member, Elem, elem_member) \
    wxl_bool WXL_CALL name(const void* object, uint32_t index, ret* out) {                 \
        const ViaType* block = object ? static_cast<const Type*>(object)->via : nullptr;   \
        if (!block || !out || index >= Extent(block->member)) {                            \
            return WXL_FALSE;                                                              \
        }                                                                                  \
        *out = Fact<ret>(block->member[index].elem_member);                                \
        return WXL_TRUE;                                                                   \
    }

#include "wxl/core/binding.def"

#undef WXL_BINDING
#undef WXL_BINDING_FIELD
#undef WXL_BINDING_FIELD_VIA
#undef WXL_BINDING_FIELD_VIA_INDEX
#undef WXL_BINDING_FIELD_VIA_AT

    // --- the call rows, one body per WXL_BINDING row of the table ----------------------------

    // --- world.def ------------------------------------------------------------------------------

    int32_t WXL_CALL map_id(void) {
        return World::QueryMapId();
    }

    // --- objectmgr.def --------------------------------------------------------------------------

    const void* WXL_CALL local_player_object(void) {
        return ClntObjMgrGetActivePlayerObj();
    }

    const void* WXL_CALL object_by_guid(uint64_t guid) {
        return ClntObjMgrObjectPtr(WOWGUID{ guid }, TYPE_OBJECT, __FILE__, __LINE__);
    }

    wxl_bool WXL_CALL object_watch_type(uint32_t type, uint32_t offset, uint32_t size, wxl_core_mirror_fn handler,
                                        void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !TakeMirror(handler)) {
            return WXL_FALSE;
        }
        ClntObjMgrSetTypeMirrorHandler(static_cast<OBJECT_TYPE_ID>(type), offset, size, &ForwardMirror, param,
                                       static_cast<HANDLER_PRIORITY>(0), 0);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL object_watch_one(uint64_t guid, uint32_t type, uint32_t offset, uint32_t size,
                                       wxl_core_mirror_fn handler, void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !object_by_guid(guid) || !TakeMirror(handler)) {
            return WXL_FALSE;
        }
        ClntObjMgrSetObjMirrorHandler(WOWGUID{ guid }, static_cast<OBJECT_TYPE_ID>(type), offset, size, &ForwardMirror,
                                      param, static_cast<HANDLER_PRIORITY>(0), 0);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL object_unwatch_type(uint32_t type, uint32_t offset, wxl_core_mirror_fn handler, void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !handler || handler != s_mirror) {
            return WXL_FALSE;
        }
        ClntObjMgrUnsetTypeMirrorHandler(static_cast<OBJECT_TYPE_ID>(type), offset, &ForwardMirror, param);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL object_unwatch_one(uint64_t guid, uint32_t type, uint32_t offset, wxl_core_mirror_fn handler,
                                         void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !handler || handler != s_mirror || !object_by_guid(guid)) {
            return WXL_FALSE;
        }
        ClntObjMgrUnsetObjMirrorHandler(WOWGUID{ guid }, static_cast<OBJECT_TYPE_ID>(type), offset, &ForwardMirror,
                                        param);
        return WXL_TRUE;
    }

    // --- object.def -----------------------------------------------------------------------------

    wxl_bool WXL_CALL object_position(const void* object, wxl_vec3* out) {
        const CGObject_C* o = Object(object);
        switch (o->m_typeId) {
            case ID_UNIT:
            case ID_PLAYER:
            case ID_GAMEOBJECT:
            case ID_DYNAMICOBJECT:
                return Give(out, o->GetPosition());
            case ID_CORPSE:
                // The fork declares no CGCorpse_C::GetPosition; the corpse's CPassenger holds it.
                return Give(out, Corpse(object)->m_passenger.m_position);
            default:
                return WXL_FALSE;
        }
    }

    wxl_bool WXL_CALL object_facing(const void* object, float* out) {
        const CGObject_C* o = Object(object);
        switch (o->m_typeId) {
            case ID_UNIT:
            case ID_PLAYER:
            case ID_GAMEOBJECT:
            case ID_DYNAMICOBJECT:
            case ID_CORPSE:
                *out = o->GetFacing();
                return WXL_TRUE;
            default:
                return WXL_FALSE;
        }
    }

    const char* WXL_CALL object_name(const void* object) {
        const CGObject_C* o = Object(object);
        switch (o->m_typeId) {
            case ID_UNIT:
            case ID_PLAYER:
            case ID_GAMEOBJECT:
            case ID_ITEM:
            case ID_CONTAINER:
                return o->GetObjectName();
            default:
                return nullptr;
        }
    }

    // --- gameui.def -----------------------------------------------------------------------------

    uint64_t WXL_CALL ui_target_guid(void) {
        return CGGameUI::m_lockedTarget.m_guid;
    }

    int32_t WXL_CALL ui_initialized(void) {
        return CGGameUI::m_initialized;
    }

    uint8_t WXL_CALL ui_logging_in(void) {
        return CGGameUI::m_loggingIn;
    }

    // --- glue.def -------------------------------------------------------------------------------

    const char* WXL_CALL glue_current_screen(void) {
        return CGlueMgr::m_currentScreen;
    }

    int32_t WXL_CALL login_server_type(const void* login) {
        return login ? const_cast<Login*>(static_cast<const Login*>(login))->GetLoginServerType() : -1;
    }

    // --- chat.def -------------------------------------------------------------------------------

    const void* WXL_CALL chat_pending_last(void) {
        return CGChat::s_pendingChat.Tail();
    }

    // --- script.def -----------------------------------------------------------------------------

    void* WXL_CALL script_state(void) {
        return FrameScript_GetContext();
    }

    wxl_bool WXL_CALL script_global_is_set(void* state, const char* name) {
        lua_State* L = State(state);
        lua_getfield(L, LUA_GLOBALSINDEX, name);
        const int type = lua_type(L, -1);
        lua_pop(L, 1);
        return type > LUA_TNIL ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL script_set_global_closure(void* state, const char* name, wxl_script_native_fn fn, uint32_t slot) {
        lua_State* L = State(state);
        lua_pushnumber(L, static_cast<lua_Number>(slot));
        lua_pushcclosure(L, reinterpret_cast<lua_CFunction>(fn), 1);
        lua_setfield(L, LUA_GLOBALSINDEX, name);
    }

    void WXL_CALL script_clear_global(void* state, const char* name) {
        lua_State* L = State(state);
        lua_pushnil(L);
        lua_setfield(L, LUA_GLOBALSINDEX, name);
    }

    uint32_t WXL_CALL script_closure_slot(void* state) {
        return static_cast<uint32_t>(lua_tonumber(State(state), lua_upvalueindex(1)));
    }

    int32_t WXL_CALL script_top(void* state) {
        return lua_gettop(State(state));
    }

    uint32_t WXL_CALL script_value_type(void* state, int32_t index) {
        switch (lua_type(State(state), index)) {
            case LUA_TNUMBER:
                return WXL_VALUE_NUMBER;
            case LUA_TSTRING:
                return WXL_VALUE_STRING;
            case LUA_TBOOLEAN:
                return WXL_VALUE_BOOL;
            default:
                return WXL_VALUE_NIL;
        }
    }

    double WXL_CALL script_to_number(void* state, int32_t index) {
        return lua_tonumber(State(state), index);
    }

    const char* WXL_CALL script_to_string(void* state, int32_t index, uint32_t* length) {
        size_t size = 0;
        const char* text = lua_tolstring(State(state), index, &size);
        if (length) {
            *length = static_cast<uint32_t>(size);
        }
        return text;
    }

    wxl_bool WXL_CALL script_to_bool(void* state, int32_t index) {
        return lua_toboolean(State(state), index) ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL script_push_number(void* state, double value) {
        lua_pushnumber(State(state), value);
    }

    void WXL_CALL script_push_string(void* state, const char* text, uint32_t length) {
        lua_pushlstring(State(state), text ? text : "", text ? length : 0u);
    }

    void WXL_CALL script_push_bool(void* state, wxl_bool value) {
        lua_pushboolean(State(state), value ? 1 : 0);
    }

    void WXL_CALL script_push_nil(void* state) {
        lua_pushnil(State(state));
    }

    void WXL_CALL script_pop(void* state, int32_t count) {
        if (count > 0) {
            lua_pop(State(state), count);
        }
    }

    void WXL_CALL script_error(void* state, const char* message) {
        lua_State* L = State(state);
        lua_pushstring(L, message ? message : "WarcraftXL: error");
        lua_error(L);
    }

    void WXL_CALL script_run(const char* source, const char* name) {
        FrameScript_Execute(source, name, nullptr);
    }

    void WXL_CALL script_create_events(const char** names, uint32_t count) {
        FrameScript_CreateEvents(names, count);
    }

    void WXL_CALL script_signal_event(uint32_t index, void* state, int32_t arg_count) {
        FrameScript_SignalEvent(index, State(state), arg_count);
    }

    // The Storage rows are the client's own file layer, called where it lives. A `file` is the
    // SFileImpl the client handed back; nothing above this knows what it is.
    SFileImpl* File(void* file) {
        return static_cast<SFileImpl*>(file);
    }

    wxl_bool WXL_CALL file_exists(const char* path) {
        return path && SFile::FileExists(path) ? WXL_TRUE : WXL_FALSE;
    }

    void* WXL_CALL file_open(const char* path) {
        SFileImpl* file = nullptr;
        return path && SFileOpenFile(path, &file) ? file : nullptr;
    }

    uint64_t WXL_CALL file_size(void* file) {
        if (!file) {
            return 0;
        }
        uint32_t high = 0;
        uint32_t low = SFileGetFileSize(File(file), &high);
        return (static_cast<uint64_t>(high) << 32) | low;
    }

    wxl_bool WXL_CALL file_seek(void* file, uint64_t offset) {
        if (!file) {
            return WXL_FALSE;
        }
        int32_t high = static_cast<int32_t>(offset >> 32);
        // Method 0 is FILE_BEGIN: the offset is absolute, which is what read_at means. The answer
        // is the cursor afterwards, 0 for a seek to the start.
        const int32_t at = SFile::SetFilePointer(File(file), static_cast<int32_t>(offset & 0xFFFFFFFFu), &high, 0);
        return wxl::core::storm::SeekLanded(static_cast<uint32_t>(at), offset) ? WXL_TRUE : WXL_FALSE;
    }

    uint32_t WXL_CALL file_read(void* file, void* buffer, uint32_t count) {
        if (!file || !buffer || !count) {
            return 0;
        }
        uint32_t read = 0;
        SFileReadFile(File(file), buffer, count, &read, nullptr, 0);
        return read;
    }

    void WXL_CALL file_close(void* file) {
        if (file) {
            SFileCloseFile(File(file));
        }
    }

    wxl_bool WXL_CALL archive_mount(const char* path, int32_t priority) {
        SArchive* archive = nullptr;
        return path && SFile::OpenArchive(path, priority, 0, &archive) ? WXL_TRUE : WXL_FALSE;
    }

    const char* WXL_CALL storage_base_path() {
        return SFile::m_basePath;
    }

    uint32_t WXL_CALL archive_list(wxl_core_name_fn fn, void* user) {
        if (!fn) {
            return 0;
        }
        // The records first: the listing opens files, which may reach the set.
        std::vector<System_SFile::ARCHIVEREC*> records;
        for (System_SFile::ARCHIVEREC* record : *System_MPQGlobals::GetArchiveList()) {
            records.push_back(record);
        }
        uint32_t listed = 0;
        for (System_SFile::ARCHIVEREC* record : records) {
            SArchive archive = {};
            archive.m_type = SARCHIVE_MPQ;
            archive.m_archive = record;
            if (record && SFile::List(&archive, fn, user)) {
                ++listed;
            }
        }
        return listed;
    }

    // --- Gx ---------------------------------------------------------------------------------
    // GxHasContext and GxIsWindowVisible test the device pointer themselves.
    // GxCapsWindowSizeInScreenCoords does not, so the size answers false without a device.

    wxl_bool WXL_CALL gx_device_exists() {
        return g_theGxDevicePtr ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL gx_has_context() {
        return GxHasContext() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL gx_window_visible() {
        return GxIsWindowVisible() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL gx_window_size(uint32_t* width, uint32_t* height) {
        if (!width || !height || !g_theGxDevicePtr) {
            return WXL_FALSE;
        }
        CRect rect = {};
        GxCapsWindowSizeInScreenCoords(rect);
        const float w = rect.maxX - rect.minX;
        const float h = rect.maxY - rect.minY;
        *width = w > 0.0f ? static_cast<uint32_t>(w + 0.5f) : 0u;
        *height = h > 0.0f ? static_cast<uint32_t>(h + 0.5f) : 0u;
        return WXL_TRUE;
    }

    // --- Memory -----------------------------------------------------------------------------
    // Each row walks the client's own lists, as wxl.footprint asks on the main thread.

    uint32_t WXL_CALL gx_resources(wxl_core_gpu_fn fn, void* user) {
        CGxDevice* device = g_theGxDevicePtr;
        if (!fn || !device || (device->m_api != GxApi_D3d9 && device->m_api != GxApi_D3d9Ex)) {
            return 0;
        }
        uint32_t told = 0;
        // A texture waiting to be created again holds a placeholder, not its texture.
        for (CGxTex* texture = device->m_textures.Head(); texture; texture = device->m_textures.Next(texture)) {
            wxl_core_gpu_resource resource = {};
            if (!texture->m_needsCreation && texture->m_apiSpecificData &&
                wxl::core::gpu::DescribeTexture(static_cast<IDirect3DBaseTexture9*>(texture->m_apiSpecificData), &resource)) {
                fn(&resource, user);
                ++told;
            }
        }
        for (CGxPool* pool = device->m_poolList.Head(); pool; pool = device->m_poolList.Next(pool)) {
            wxl_core_gpu_resource resource = {};
            if (pool->m_apiSpecific &&
                wxl::core::gpu::DescribeBuffer(static_cast<IDirect3DResource9*>(pool->m_apiSpecific), &resource)) {
                fn(&resource, user);
                ++told;
            }
        }
        return told;
    }

    wxl_bool WXL_CALL sound_heap(uint64_t* current, uint64_t* peak) {
        int now = 0;
        int most = 0;
        if (!current || !peak || FMOD_Memory_GetStats(&now, &most) != FMOD_OK) {
            return WXL_FALSE;
        }
        *current = static_cast<uint32_t>(now);
        *peak = static_cast<uint32_t>(most);
        return WXL_TRUE;
    }

    uint64_t WXL_CALL script_heap() {
        lua_State* L = FrameScript_GetContext();
        if (!L) {
            return 0;
        }
        return static_cast<uint64_t>(lua_gc(L, LUA_GCCOUNT, 0)) * 1024u + static_cast<uint64_t>(lua_gc(L, LUA_GCCOUNTB, 0));
    }

    namespace {
        template <class T>
        uint64_t Reserved(const std::vector<T>& vector) {
            return static_cast<uint64_t>(vector.capacity()) * sizeof(T);
        }

        uint64_t PackedBytes(const System_SFile::BitPacked* bits) {
            return bits ? (static_cast<uint64_t>(bits->m_bitCount) + 7) / 8 + sizeof(*bits) : 0;
        }

        void Describe(const System_SFile::ARCHIVEREC* rec, wxl_core_archive* out) {
            out->path = wxl::core::storm::ArchiveName(rec->m_name, rec->m_directPath.c_str());
            out->priority = rec->m_priority;
            out->hash_entries = rec->m_header.m_hashTableSize;
            out->block_entries = rec->m_header.m_blockTableSize;
            uint64_t bytes = Reserved(rec->m_hashTable) + Reserved(rec->m_blockTable);
            if (const System_SFile::BlockEntryTable* blocks = rec->m_blockEntryTable) {
                bytes += sizeof(*blocks) + Reserved(blocks->m_flagTable) + Reserved(blocks->m_fileTimes) +
                         Reserved(blocks->m_crcs) + Reserved(blocks->m_md5s) + PackedBytes(blocks->m_hashes) +
                         PackedBytes(blocks->m_entries);
            }
            if (const System_SFile::HashEntryTable* hashes = rec->m_hashEntryTable) {
                bytes += sizeof(*hashes) + hashes->m_slotCount + PackedBytes(hashes->m_indices);
            }
            // A listfile name: a node of the map, and the name's own block when it outgrows the string.
            for (const auto& name : rec->m_listfile) {
                bytes += sizeof(name) + 4 * sizeof(void*) + (name.first.capacity() > 15 ? name.first.capacity() + 1 : 0);
            }
            out->bytes = bytes;
        }
    }

    uint32_t WXL_CALL archive_tables(wxl_core_archive_fn fn, void* user) {
        if (!fn) {
            return 0;
        }
        uint32_t told = 0;
        for (System_SFile::ARCHIVEREC* record : *System_MPQGlobals::GetArchiveList()) {
            if (!record) {
                continue;
            }
            wxl_core_archive archive = {};
            Describe(record, &archive);
            fn(&archive, user);
            ++told;
        }
        return told;
    }

    wxl_bool WXL_CALL archive_of(void* archive, wxl_core_archive* out) {
        const SArchive* handle = static_cast<const SArchive*>(archive);
        if (!handle || !out || handle->m_type != SARCHIVE_MPQ || !handle->m_archive) {
            return WXL_FALSE;
        }
        Describe(handle->m_archive, out);
        return WXL_TRUE;
    }

    namespace {
        template <class Rec>
        void TellDb(const WowClientDB<Rec>& store, const char* file, wxl_core_db_fn fn, void* user) {
            const uint64_t records = store.m_numRecords > 0 ? static_cast<uint64_t>(store.m_numRecords) : 0;
            const uint64_t ids = records && store.m_maxID >= store.m_minID ? static_cast<uint64_t>(store.m_maxID - store.m_minID) + 1 : 0;
            wxl_core_db db = {};
            db.file = file;
            db.loaded = store.m_loaded ? WXL_TRUE : WXL_FALSE;
            db.records = static_cast<uint32_t>(records);
            db.record_size = sizeof(Rec);
            db.blocks[0] = store.m_records;
            db.estimates[0] = records * sizeof(Rec);
            db.blocks[1] = store.m_strings;
            db.blocks[2] = store.m_recordsById;
            db.estimates[2] = ids * sizeof(Rec*);
            fn(&db, user);
        }

        template <class Rec>
        void TellDb(const WowClientDB_RLE<Rec>& store, const char* file, wxl_core_db_fn fn, void* user) {
            const uint64_t records = store.m_numRecords > 0 ? static_cast<uint64_t>(store.m_numRecords) : 0;
            const uint64_t ids = records && store.m_maxID >= store.m_minID ? static_cast<uint64_t>(store.m_maxID - store.m_minID) + 1 : 0;
            wxl_core_db db = {};
            db.file = file;
            db.loaded = store.m_loaded ? WXL_TRUE : WXL_FALSE;
            db.records = static_cast<uint32_t>(records);
            db.record_size = sizeof(Rec);
            db.blocks[0] = store.m_data;
            db.estimates[0] = records * sizeof(Rec);
            db.blocks[1] = store.m_strings;
            // The row pointers, and the index by id after them in the same block.
            db.blocks[2] = store.m_records;
            db.estimates[2] = (records + ids) * sizeof(Rec*);
            fn(&db, user);
        }
    }

    uint32_t WXL_CALL client_dbs(wxl_core_db_fn fn, void* user) {
        if (!fn) {
            return 0;
        }
        uint32_t told = 0;
#define WXL_CLIENT_DB(global, Rec, store, file, row_size) \
    TellDb(global, file, fn, user);                        \
    ++told;
#include "wxl/core/client_dbs.def"
#undef WXL_CLIENT_DB
        return told;
    }

    // --- Asset: models, map objects, doodads (asset.def) -------------------------------------

    const char* WXL_CALL model_path(const void* shared) {
        return shared ? static_cast<const CM2Shared*>(shared)->m_fileName : nullptr;
    }

    const void* WXL_CALL model_bytes(const void* shared, uint32_t* size) {
        if (!shared) {
            return nullptr;
        }
        const CM2Shared* model = static_cast<const CM2Shared*>(shared);
        if (size) {
            *size = model->m_fileSize;
        }
        return model->m_data;
    }

    wxl_bool WXL_CALL model_replace_bytes(void* shared, const void* bytes, uint32_t size) {
        if (!shared || !bytes || !size) {
            return WXL_FALSE;
        }
        void* copy = SIMD::AllocA16(size, __FILE__, __LINE__);
        if (!copy) {
            return WXL_FALSE;
        }
        std::memcpy(copy, bytes, size);
        CM2Shared* model = static_cast<CM2Shared*>(shared);
        M2Data* read = model->m_data;
        model->m_data = static_cast<M2Data*>(copy);
        model->m_fileSize = size;
        if (read) {
            SIMD::FreeA16(read);
        }
        return WXL_TRUE;
    }

    const char* WXL_CALL map_obj_path(const void* map_obj) {
        return map_obj ? static_cast<const CMapObj*>(map_obj)->m_wmoName : nullptr;
    }

    const void* WXL_CALL map_obj_bytes(const void* map_obj, uint32_t* size) {
        if (!map_obj) {
            return nullptr;
        }
        const CMapObj* root = static_cast<const CMapObj*>(map_obj);
        if (size) {
            *size = static_cast<uint32_t>(root->m_wmoFileSize);
        }
        return root->m_pWmoData;
    }

    wxl_bool WXL_CALL doodad_position(const void* def, wxl_vec3* out) {
        if (!def || !out) {
            return WXL_FALSE;
        }
        return Give(out, static_cast<const CMapDoodadDef*>(def)->m_position);
    }
}
