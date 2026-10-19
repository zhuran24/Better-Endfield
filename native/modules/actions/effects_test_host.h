// Exercise production restoration/finish paths with a minimal managed API
// double. This does not simulate Unity particle rendering or the game.
#pragma once
#include "module.cpp"
#include <cstdlib>
#include <vector>
using namespace BetterEndfieldNext::Actions;
#include "actions_test_support.h"
namespace {
struct Particle { bool alive = true; bool loop = true; float duration = 2.5f; int stops = 0; };
std::vector<uint32_t> freed;
int unity_calls = 0, finish_calls = 0, stop_calls = 0;
int follow_calls = 0, action_calls = 0, mesh_calls = 0;
float duration_delta = -1;
Particle* enumerated_particle = nullptr;
const char* effect_name = nullptr;
int visual_identity = 0, visual_handle = 0, visual_other = 0;
void* action_handle = &visual_handle;
unsigned visual_logic = 20;
int visual_actor = 4;
float visual_delay = 1.5667f;
bool moving = true, airborne = false;
bool main_character = true, visual_metadata_error = false;
bool animator_loop = false;
MethodId failing_method = MethodCount;
bool invoke_returns_null = false;
int state_loop_calls = 0;
const char* perform_id = "CharIntPerform_Liino_Spdash";
Particle* finish_particle = nullptr;
void* BE_CALL InvokeFake(void*, const void* method, void* instance, void** args, void** exception) {
    *exception = nullptr;
    ++unity_calls;
    auto id = static_cast<MethodId>(reinterpret_cast<uintptr_t>(method) - 1);
    if (id == StateLoop) ++state_loop_calls;
    if (id == failing_method) {
        if (!invoke_returns_null) *exception = &visual_other;
        return nullptr;
    }
    auto* p = static_cast<Particle*>(instance);
    switch (id) {
    case GetEntity: case GetCharCtrl: case GetMovement: case GetGait:
    case GetBlackboard: case GetAnimator: case CurrentState: return &visual_identity;
    case EntityValid: { static bool yes = true; return &yes; }
    case IsMain: return &main_character;
    case VisualPerformId:
        if (visual_metadata_error) { *exception = &visual_other; return nullptr; }
        return const_cast<char*>(instance == &visual_handle ? perform_id : "OtherPerform");
    case VisualPerformEntity: return &visual_identity;
    case Moving: return &moving;
    case Airborne: return &airborne;
    case IsDead: case InTransition: { static bool no = false; return &no; }
    case StateLoop: return &animator_loop;
    case MoveMode: return &g_dash_mode;
    case GaitValue: return &g_sprint;
    case StateHash: return &g_hashes[0];
    case StateTime: { static float time = .5f; return &time; }
    case StateLength: { static float length = 173.f/60; return &length; }
    case EffectObject: return p;
    case EffectName: return const_cast<char*>(effect_name);
    case ComponentsInChildren: return enumerated_particle;
    case ArrayLength: { static int count = 1; return &count; }
    case ArrayItem: return enumerated_particle;
    case EffectManualFollow: ++follow_calls; return nullptr;
    case UnityAlive: return &static_cast<Particle*>(args[0])->alive;
    case ParticleMain: return p;
    case ParticleLoop: return &p->loop;
    case ParticleDuration: return &p->duration;
    case ParticlePlay: return nullptr;
    case ParticleStop: ++p->stops; return nullptr;
    case ParticleSetLoop: p->loop = *static_cast<bool*>(args[0]); return nullptr;
    case ParticleSetDuration: p->duration = *static_cast<float*>(args[0]); return nullptr;
    default: CHECK(false); return nullptr;
    }
}
void* BE_CALL UnboxFake(void*, void* value) { return value; }
int BE_CALL CopyStringFake(void*, const void* value, char* output, size_t capacity) {
    return std::snprintf(output, capacity, "%s", static_cast<const char*>(value));
}
void BE_CALL FreeFake(void*, uint32_t handle) { freed.push_back(handle); }
uint32_t BE_CALL PinFake(void*, void*, int) { return 101; }
void* BE_CALL FieldFake(void*, const void* field, void*) {
    switch (static_cast<FieldId>(reinterpret_cast<uintptr_t>(field) - 1)) {
    case Request: return &visual_identity;
    case RequestHandle: return &visual_handle;
    case VisualActionHandle: return action_handle;
    case VisualActionData: return &visual_identity;
    case ActionLogic: return &visual_logic;
    case ActionIndex: return &visual_actor;
    case ActionDelay: return &visual_delay;
    default: CHECK(false); return nullptr;
    }
}
void __fastcall ActionFake(void*, const void*) { ++action_calls; }
void __fastcall MeshFake(void*, void*, int, bool, const void*) { ++mesh_calls; }
void __fastcall FinishFake(void*, bool, bool, const void*) {
    ++finish_calls;
    CHECK(!finish_particle->loop && finish_particle->duration == 5.0f);
}
void __fastcall StopFake(void*, const void*) { ++stop_calls; }
void __fastcall DurationFake(void*, float delta, const void*) { duration_delta = delta; }
EffectHold Held(Particle& p, void* instance) {
    EffectHold e;
    e.object = instance; e.root = 102; e.prepared = true; e.count = 1;
    e.particles[0] = {&p, 101, false, 5.0f};
    return e;
}
}
