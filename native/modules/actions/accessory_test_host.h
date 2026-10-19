#pragma once
#include "actions_test_support.h"
#include <Windows.h>
#include <functional>
#include <map>
namespace AccessoryTest {
inline DWORD live_thread = 1;
inline double real_seconds = 10;
inline bool clock_ok = true;
inline unsigned clock_calls = 0;
DWORD ModulePath(HMODULE, wchar_t *, DWORD);
inline DWORD ThreadId() {
    return live_thread;
}
inline BOOL Frequency(LARGE_INTEGER *out) {
    ++clock_calls;
    out->QuadPart = 1000000;
    return clock_ok;
}
inline BOOL Counter(LARGE_INTEGER *out) {
    ++clock_calls;
    out->QuadPart = static_cast<int64_t>(real_seconds * 1000000 + .5);
    return clock_ok;
}
} // namespace AccessoryTest
// Only the platform clock/thread boundary is replaced. All module declarations,
// parsing, ownership, pose setters and lifecycle code below are production code.
#if defined(_WIN32)
#define GetModuleFileNameW AccessoryTest::ModulePath
#endif
#define GetCurrentThreadId AccessoryTest::ThreadId
#define QueryPerformanceFrequency AccessoryTest::Frequency
#define QueryPerformanceCounter AccessoryTest::Counter
#include "module.cpp"
#if defined(_WIN32)
#undef GetModuleFileNameW
#endif
#undef GetCurrentThreadId
#undef QueryPerformanceFrequency
#undef QueryPerformanceCounter
#include "accessory_table_fixture.h"
namespace AccessoryTest {
using namespace BetterEndfieldNext::Actions;
using Method = MethodId;
std::filesystem::path test_root;
DWORD ModulePath(HMODULE, wchar_t *out, DWORD size) {
    auto path = (test_root / "test.dll").wstring();
    if (path.size() >= size)
        return 0;
    std::copy(path.begin(), path.end(), out);
    out[path.size()] = 0;
    return static_cast<DWORD>(path.size());
}
Frame live_frame;
int live_hash = -445642423, live_frame_id = 1;
struct Node {
    std::string name;
    BonePose pose{};
    unsigned writes = 0;
    bool alive = true, child = true, write_ok = true, read_ok = true;
};
Node root{"root"}, tail{"tail_01_jnt"}, spare{"spare"}, duplicate{"tail_01_jnt"};
void *live_animator = &root;
void *live_root = &root;
void *live_entity = &root;
bool entity_valid = true, is_main = true, helpers_ok = true, frame_count_ok = true;
std::function<void()> on_pose_read, on_pose_write;
std::function<void(Method)> on_method;
std::function<void(FieldId)> on_field;
std::vector<Node *> tree{&root, &tail, &spare};
int component_token, handle_token, next_handle_token;
constexpr uintptr_t kHashFieldBase = 0x5000;
constexpr uintptr_t kContainsKeyMethod = 0x6000, kDictionaryItemMethod = 0x6001;
std::string template_id = "chr_0036_jsspsi", perform_name = "CharIntPerform_Jsspsi_SpdashR";
bool companions_available = false;
int companion_dictionary = 0;
void __fastcall NativeStart(void *, int, const void *) { CHECK(!g_pose_call_scope && !g_dash_call_scope); }
void __fastcall NativeRelease(void *, bool, const void *) {}
BE_Result BE_CALL ReleaseHooks(void *, const char *) {
    return BE_Result_Ok;
}
void *FakeClass(void *) {
    return &root;
}
void *FakeParent(void *) {
    return nullptr;
}
const void *FakeMethod(void *, const char *name, int) {
    return reinterpret_cast<const void *>(std::string_view(name) == "ContainsKey" ? kContainsKeyMethod
                                                                                  : kDictionaryItemMethod);
}
void *requested_handle = &handle_token;
unsigned managed_calls = 0, pin_count = 0, pin_serial = 0;
int pin_fail_after = -1;
bool throw_pin = false;
std::vector<std::string> logs;
struct LeaseHost {
    bool available = true;
    uint64_t token = 0, serial = 40;
    unsigned releases = 0, acquires = 0, owns_calls = 0;
    std::map<const void *, uint64_t> companions;
    uint64_t Acquire(const void *r) {
        ++acquires;
        if (!available || (r == &root ? token != 0 : companions.contains(r)))
            return 0;
        uint64_t t = ++serial;
        if (r == &root)
            token = t;
        else
            companions[r] = t;
        return t;
    }
    bool Owns(const void *r, uint64_t t) {
        ++owns_calls;
        return t && (r == &root ? token == t : companions.contains(r) && companions.at(r) == t);
    }
    int Release(const void *r, uint64_t t) {
        if (!Owns(r, t))
            return 0;
        if (r == &root)
            token = 0;
        else
            companions.erase(r);
        ++releases;
        return 1;
    }
} lease_host;
uint64_t BE_CALL Acquire(const void *r, const char *) {
    return lease_host.Acquire(r);
}
int BE_CALL Owns(const void *r, const char *, uint64_t t) {
    return lease_host.Owns(r, t);
}
int BE_CALL Release(const void *r, const char *, uint64_t t) {
    return lease_host.Release(r, t);
}
BE_PoseLeaseApiV1 leases{1, Acquire, Owns, Release};
void BE_CALL LogFake(void *, const char *, const char *text) {
    logs.emplace_back(text);
}
uint32_t BE_CALL Pin(void *, void *, int) {
    if (throw_pin)
        throw std::bad_alloc();
    if (pin_fail_after == 0)
        return 0;
    if (pin_fail_after > 0)
        --pin_fail_after;
    ++pin_count;
    return ++pin_serial;
}
void BE_CALL Free(void *, uint32_t) {
    CHECK(pin_count > 0);
    --pin_count;
}
void *BE_CALL Unbox(void *, void *p) {
    return p;
}
int BE_CALL Copy(void *, const void *p, char *out, size_t size) {
    const auto &s = *static_cast<const std::string *>(p);
    std::snprintf(out, size, "%s", s.c_str());
    return int(s.size());
}
void *BE_CALL Field(void *, const void *f, void *) {
    static int hashes[]{528430122, -445642423, 33, 44};
    const auto pointer = uintptr_t(f);
    if (pointer >= kHashFieldBase && pointer < kHashFieldBase + 4)
        return &hashes[pointer - kHashFieldBase];
    const auto id = FieldId(pointer - 1);
    if (on_field) on_field(id);
    if (id == TemplateId)
        return &template_id;
    if (id == JsspsiTmpObjects && companions_available)
        return &companion_dictionary;
    if (id == Request)
        return &component_token;
    if (id == RequestHandle)
        return requested_handle;
    return nullptr;
}
void *BE_CALL InvokeFake(void *, const void *m, void *object, void **args, void **exception) {
    ++managed_calls;
    *exception = nullptr;
    static bool yes = true, no_value = false;
    if (uintptr_t(m) == kContainsKeyMethod)
        return &yes;
    if (uintptr_t(m) == kDictionaryItemMethod)
        return &root;
    const auto id = MethodId(uintptr_t(m) - 1);
    if (on_method)
        on_method(id);
    if (!helpers_ok) {
        *exception = &component_token;
        return nullptr;
    }
    static int count, other_hash = 12345;
    static int next_state;
    static bool no = false;
    static float length = 5.667f;
    switch (id) {
    case GetTemplate:
    case GetBlackboard:
    case GetMovement:
    case GetGait:
        return &root;
    case IsDead:
    case Airborne:
        return &no_value;
    case Moving:
    case IsPlayingPerform:
        return &yes;
    case MoveMode:
        return &g_dash_mode;
    case GaitValue:
        return &g_sprint;
    case PerformId:
        return &perform_name;
    case CrossFade:
    case JsspsiStopPlaying:
        return nullptr;
    case TransformParent:
        return &root;
    case JsspsiTmpAnimator:
        return &root;
    case GetAnimator:
        return live_animator;
    case PoseComponentTransform:
        return live_root;
    case GetEntity:
        return live_entity;
    case GetCharCtrl:
        return &root;
    case EntityValid:
        return &entity_valid;
    case IsMain:
        return &is_main;
    case UnityAlive:
        return &static_cast<Node *>(args[0])->alive;
    case PoseFrameCount:
        if (!frame_count_ok) {
            *exception = &component_token;
            return nullptr;
        }
        return &live_frame_id;
    case ClipName:
        return &static_cast<Node *>(object)->name;
    case ComponentGameObject:
        return object;
    case ComponentsInChildren:
        return &tree;
    case ArrayLength:
        count = int(tree.size());
        return &count;
    case ArrayItem:
        return tree.at(*static_cast<int *>(args[0]));
    case PoseIsChild:
        return &static_cast<Node *>(object)->child;
    case InTransition:
        return &live_frame.transitioning;
    case CurrentState:
        return &live_frame;
    case NextState:
        return &next_state;
    case StateHash:
        return object == &next_state ? (live_frame.outgoing ? &other_hash : &live_hash)
                                     : (live_frame.current_special ? &live_hash : &other_hash);
    case StateTime:
        return object == &next_state ? &live_frame.next_time : &live_frame.time;
    case StateLength:
        return &length;
    case StateLoop:
        return &no;
    case PoseGetLocal: {
        auto *n = static_cast<Node *>(object);
        *static_cast<PoseVector *>(args[0]) = n->pose.position;
        *static_cast<PoseQuaternion *>(args[1]) = n->pose.rotation;
        if (on_pose_read)
            on_pose_read();
        if (!n->read_ok)
            *exception = &component_token;
        return nullptr;
    }
    case PoseSetLocal: {
        auto *n = static_cast<Node *>(object);
        if (!n->write_ok) {
            *exception = &component_token;
            return nullptr;
        }
        n->pose = {*static_cast<PoseQuaternion *>(args[1]), *static_cast<PoseVector *>(args[0])};
        ++n->writes;
        if (on_pose_write)
            on_pose_write();
        return nullptr;
    }
    default:
        CHECK(false);
        return nullptr;
    }
}
BE_HostApiV1 host{};
const CharacterProfile *Profile() {
    for (const auto &p : kCharacters)
        if (p.accessory_file)
            return &p;
    std::abort();
}
void InstallHost() {
    host.runtime_invoke = InvokeFake;
    host.object_unbox = Unbox;
    host.copy_managed_string = Copy;
    host.gchandle_new = Pin;
    host.gchandle_free = Free;
    host.field_get_value_object = Field;
    host.log = LogFake;
    g_host = &host;
    for (int i = 0; i < MethodCount; ++i)
        g_methods[i].resolved.method_info = reinterpret_cast<void *>(uintptr_t(i + 1));
    for (int i = 0; i < FieldCount; ++i)
        g_fields[i].resolved.field_info = reinterpret_cast<void *>(uintptr_t(i + 1));
    g_start = NativeStart;
    g_release = NativeRelease;
    host.release_module_hooks = ReleaseHooks;
    g_object_class = FakeClass;
    g_class_parent = FakeParent;
    g_class_method = FakeMethod;
    g_dash_mode = 1;
    g_sprint = 1;
    for (int i = 0; i < 2; ++i) {
        g_hash_fields[i] = reinterpret_cast<void *>(kHashFieldBase + i);
        g_locomotion_fields[i] = reinterpret_cast<void *>(kHashFieldBase + 2 + i);
    }
    g_transform_type = &root;
    g_pose_leases = &leases;
    g_hashes[0] = 528430122;
    g_hashes[1] = -445642423;
#if !defined(_WIN32)
    setenv("BETTER_ENDFIELD_NEXT_ACTIONS_ASSET_ROOT", (test_root / "actions").c_str(), 1);
#endif
}
void Cleanup() {
    on_pose_read = {};
    on_pose_write = {};
    on_method = {};
    on_field = {};
    EndAccessory();
    Cancel("test cleanup", false, false);
    ReleaseSeam();
    g_dash_pose.reset();
    ReleasePoseOverlay(false);
    DiscardCapture();
}
void WriteTable(bool bad = false) {
    auto bytes = Make().bytes;
    if (bad)
        bytes[0] = 'x';
    const auto path = test_root / "actions/accessory_jsspsi.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), bytes.size());
    }
    static int revision = 0;
    std::filesystem::last_write_time(path,
                                     std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++revision));
}
std::shared_ptr<Configuration> Reset(bool enabled = true) {
    Cleanup();
    CHECK(pin_count == 0);
    root = Node{"root"};
    tail = Node{"tail_01_jnt"};
    spare = Node{"spare"};
    duplicate = Node{"tail_01_jnt"};
    tree = {&root, &tail, &spare};
    helpers_ok = frame_count_ok = true;
    live_thread = 1;
    pin_fail_after = -1;
    g_pose_contract = true;
    g_hierarchy_contract = true;
    live_animator = live_root = live_entity = &root;
    entity_valid = is_main = true;
    real_seconds = 10;
    clock_ok = true;
    lease_host = {};
    g_pose_owner = {};
    g_pose_leases = &leases;
    live_frame = {};
    live_frame.time = .30f;
    live_frame.current_special = true;
    live_hash = -445642423;
    ++live_frame_id;
    companions_available = false;
    g_start = NativeStart;
    requested_handle = &handle_token;
    g_stopping = false;
    g_game_thread = 1;
    auto config = std::make_shared<Configuration>();
    config->enabled = true;
    config->jsspsi_accessory_loop = enabled;
    config->jsspsi_accessory_fade = .2f;
    g_session.component = &component_token;
    g_session.handle = &handle_token;
    g_session.config = config;
    g_config.store(config);
    g_session.profile = Profile();
    g_session.policy.active = true;
    g_session.loop.c = kJsspsiLoop;
    g_capture = {};
    g_dash_pose = std::make_shared<DashPoseContext>();
    g_dash_pose->component = g_session.component;
    g_dash_pose->handle = g_session.handle;
    g_dash_pose->diagnostics = true;
    if (enabled)
        BeginAccessory(g_session.component);
    return config;
}
void Rewound() {
    g_session.rewound = true;
    g_session.animation_replays = 1;
}
void Tick() {
    AccessoryPrepareFrame(g_session.component);
    SeamFixFrame(g_session.component);
    AccessoryApplyFrame(g_session.component);
    AccessoryFadeFrame(g_session.component);
}
// Exercise the same lifecycle entry used by interruption and native completion.
void FinishDash(bool fade = true) {
    if (!fade)
        EndAccessory();
    Cancel("test dash ended", false, false);
}
void NewStart() {
    requested_handle = &next_handle_token;
    StartDetour(&component_token, 0, nullptr);
    CHECK(g_session.handle == requested_handle && g_dash_pose && !g_session.features_failed);
}

void FadeTick(double elapsed) {
    real_seconds = 10 + elapsed;
    ++live_frame_id;
    AccessoryFadeFrame(&component_token);
}
void SeamReady() {
    g_seam = std::make_shared<SeamState>();
    g_seam->handle = g_session.handle;
    g_seam->lease_context = g_dash_pose;
    auto &o = g_seam->objects[0];
    o.bound = true;
    o.animator = &root;
    o.root = &root;
    o.lease = AcquireDashRoot(g_dash_pose, 0, &root);
    o.mode = SeamMode::Apply;
    o.entry = 0;
    o.hash = o.seen_hash = live_hash;
    o.target = .126f;
    o.begin = .315f;
    o.last_time = .29f;
    for (auto *n : {&tail, &spare}) {
        BoundBone b;
        b.transform = n;
        b.name = n->name;
        o.bones.push_back(std::move(b));
    }
    SeamEntry entry;
    entry.hash = live_hash;
    entry.target = .126f;
    entry.begin = .315f;
    for (auto *n : {&tail, &spare})
        entry.bones[n->name] = {{}, {1, 0, 0}};
    g_seam_table = std::make_shared<SeamTable>(SeamTable{entry});
}
} // namespace AccessoryTest
