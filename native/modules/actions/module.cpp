#include "BetterEndfieldNext/ModuleApi.h"
#include "BetterEndfieldNext/PoseLease.h"
#include "dash_policy.h"
#include "pose_policy.h"
#include "accessory_table.h"
#include "seam_table.h"
#include <fstream>

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <optional>
#include <functional>
#include <cstdlib>

namespace BetterEndfieldNext::Actions {
namespace {
constexpr char kId[] = "betterendfieldnext.actions";
constexpr char kGame[] = "Gameplay.Beyond.dll";
constexpr char kUnity[] = "UnityEngine.AnimationModule.dll";
constexpr char kUnityCore[] = "UnityEngine.CoreModule.dll";
constexpr char kParticles[] = "UnityEngine.ParticleSystemModule.dll";
constexpr char kView[] = "Beyond.Gameplay.View";
constexpr char kCore[] = "Beyond.Gameplay.Core";
constexpr char kAnimation[] = "Beyond.Gameplay.View.Animation";
constexpr char kCharacterComp[] = "CharacterAnimationComponent";
constexpr char kComplex[] = "ComplexAnimatorComponent";
constexpr char kBrain[] = "CharacterSpecialDashBrain";
constexpr char kPerformNs[] = "Beyond.Gameplay.Core.CharInteractPerform";
constexpr char kHandle[] = "CharPerformHandleBase";
constexpr size_t kEffectSlotCount = 22;

// Everything character specific lives here; the hooks themselves are shared.
// A character with no continuous VFX or deferred prop hide simply leaves those
// entries empty, and the corresponding hold is skipped for that session.
struct CharacterProfile {
    const char* codename;
    const char* template_id;
    const char* perform_id;
    const wchar_t* pose_file;
    LoopConstants loop;
    // Perform effects whose own lifetime is shorter than a held dash, indexed by
    // the actorIndex the perform creates them under. Empty slots are skipped.
    const char* effects[kEffectSlotCount];
    // Aglina's second effect is a single 3 s burst inside a 5 s system; renewing
    // it needs a shorter particle duration. No other character has that shape.
    bool renew_short_glow;
    const char* flying_stop_event;
    PropHideRule prop_hide;
    // Some performs hide model parts partway through the clip (Liino stows the
    // board at 2.2 s). Deferring those hides keeps the parts visible while the
    // dash repeats; the native Clear still restores them on exit.
    bool defer_mesh_group_hide;
    // Accept the exact perform name or one trailing L/R.
    bool side_suffixed_perform = false;
    // Synchronize temporary companion 0 to the rider rewind.
    bool rewind_mount = false;
    // Independent window for temporary companions 1 and 2.
    const LoopConstants* side_loop = nullptr;
    // Actor masks are local to the owned perform; Jsspsi uses unnamed beast effects.
    uint32_t held_effect_actors = 0;
    // Ending-only effects suppressed before native OnPlay (capture is exempt).
    uint32_t skipped_effect_actors = 0;
    // Freeze collected beast-render clocks after the first rewind.
    bool freeze_render_vfx = false;
    // Stop the captured startup event at the first rewind.
    bool stop_start_sound = false;
    // Learned BESEAM03 endpoint corrections, one token per root.
    const char* seam_file = nullptr;
    // BEACC001 phase-sampled overlay, released through an exit fade.
    const char* accessory_file = nullptr;
    // BECAP001 native-clip recording output when capture is selected.
    const char* capture_file = nullptr;
    // Name token identifying owned VFX and character audio sources.
    const char* owned_name_token = nullptr;
    // Additional audio-source token for companion objects.
    const char* companion_name_filter = nullptr;
};
constexpr CharacterProfile kCharacters[]{
    {"aglina", "chr_0013_aglina", "CharIntPerform_Aglina_Spdash", L"pose_aglina.bin", kAglinaLoop,
     {"P_fxbat_aglina_sprint_dash_sp_01", "P_fxbat_aglina_sprint_dash_sp_02", nullptr, nullptr}, true,
     "Aglina_SprintDashSp_Flying_Stop", kAglinaPropHide, false},
    // Liino's board glow is one prefab mounted under each foot (actor 0 and 1)
    // plus the trail at actor 2. Keep the mounted weapon exhaust/glow (8-13)
    // and pulse (20/21), never the body-wide accents or teardown effects.
    {"liino", "chr_0035_liino", "CharIntPerform_Liino_Spdash", L"pose_liino.bin", kLiinoLoop,
     {"P_fxbat_liino_sprint_dash_sp_01", "P_fxbat_liino_sprint_dash_sp_01",
      "P_fxbat_liino_sprint_dash_sp_02",
      nullptr, nullptr, nullptr, nullptr, nullptr, // 3-7: material/foot entry accents
      "P_fxbat_liino_sprint_dash_sp_weapons_base_L_a_01_jnt_start_01",
      "P_fxbat_liino_sprint_dash_sp_weapons_base_R_a_01_jnt_start_01",
      "P_fxbat_liino_sprint_dash_sp_weapons_base_L_a_01_jnt_start_02",
      "P_fxbat_liino_sprint_dash_sp_weapons_base_R_a_01_jnt_start_02",
      "P_fxbat_liino_sprint_dash_sp_slot_L_potentialEffect_01_vfx_start_01",
      "P_fxbat_liino_sprint_dash_sp_slot_R_potentialEffect_01_vfx_start_01",
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, // 14-19: exit/body accents
      "P_fxbat_liino_sprint_dash_sp_weapons_base_L_a_01_jnt_lvdong",
      "P_fxbat_liino_sprint_dash_sp_weapons_base_R_a_01_jnt_lvdong"}, false,
     nullptr, {}, true},
    // Jsspsi keeps the native Animator: no full-pose bank. Beast 0 follows
    // her rewind; beasts 1 and 2 use the independent side window.
    {
     /*codename=*/"jsspsi",
     /*template_id=*/"chr_0036_jsspsi",
     /*perform_id=*/"CharIntPerform_Jsspsi_Spdash",
     /*pose_file=*/nullptr,
     /*loop=*/kJsspsiLoop,
     /*effects=*/{},
     /*renew_short_glow=*/false,
     /*flying_stop_event=*/nullptr,
     /*prop_hide=*/{},
     /*defer_mesh_group_hide=*/true,
     /*side_suffixed_perform=*/true,
     /*rewind_mount=*/true,
     /*side_loop=*/&kJsspsiSideLoop,
     /*held_effect_actors=*/kJsspsiHoldEffects,
     /*skipped_effect_actors=*/kJsspsiSkipEffects,
     /*freeze_render_vfx=*/true,
     /*stop_start_sound=*/true,
     /*seam_file=*/"seam_jsspsi.bin",
     /*accessory_file=*/"accessory_jsspsi.bin",
     /*capture_file=*/"capture_jsspsi.bin",
     /*owned_name_token=*/"jsspsi",
     /*companion_name_filter=*/"inkbiter"},
};
constexpr size_t kCharacterCount = std::size(kCharacters);

struct Configuration {
    bool enabled = false;
    bool diagnostics = true;
    bool external_loop = false; // v12: external bone-pose data, not AnimationClip bundles.
    bool liino_clean = false; // Optional dash features default off.
    // One bit per kCharacters entry. A configuration without the key predates the
    // per-character switches and keeps every supported character armed.
    uint32_t characters = (1u << kCharacterCount) - 1;
    bool jsspsi_capture = false;
    bool jsspsi_seam_fix = true;
    bool jsspsi_accessory_loop = true;
    uint32_t jsspsi_accessory_mask = 0x03;
    float jsspsi_accessory_fade = .4f;
    bool operator==(const Configuration&) const = default;
};
#if defined(_WIN32)
using ConfigurationSlot = std::atomic<std::shared_ptr<const Configuration>>;
#else
// libc++ on the Android NDK does not ship the C++20 std::atomic<shared_ptr>
// specialization, so the primary template rejects a non-trivially-copyable type.
// This slot has the same two operations the module uses: a store from
// configuration_changed, and a load from the detours. Publishes are rare (one per
// settings change) and a load is a handful of times per dashing frame, so the
// mutex is not on any path where its cost is measurable.
class ConfigurationSlot {
public:
    explicit ConfigurationSlot(std::shared_ptr<const Configuration> initial)
        : value_(std::move(initial)) {}
    std::shared_ptr<const Configuration> load() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }
    void store(std::shared_ptr<const Configuration> next) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = std::move(next);
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const Configuration> value_;
};
#endif
ConfigurationSlot g_config{std::make_shared<Configuration>()};
const BE_HostApiV1* g_host = nullptr;
std::atomic_bool g_stopping{false};
std::atomic<DWORD> g_game_thread{0};

enum MethodId {
    Start, PreLate, ShouldInterrupt, Interrupt, ForceStop, ForceCurrent, Release,
    GetEntity, EntityValid, GetTemplate, GetCharCtrl, IsMain, GetMovement,
    GetGait, GaitValue, GetBlackboard, IsDead, GetAnimator,
    CurrentState, NextState, InTransition, StateHash, StateTime, StateLength,
    IsPlayingPerform, PerformId, SetDashing, MoveMode, Moving, Airborne,
    FlowTick, PerformClear, PerformState, HandleAlive, StateLoop, AddCommand,
    CrossFade, TryInterruptCommand, DoExit, ShowObject,
    AudioMonoPost, AudioMonoComponent, AudioEntityPost, AudioHash,
    EffectName, EffectObject, EffectDuration, EffectFinish, EffectStop,
    ComponentsInChildren, ArrayLength, ArrayItem, ParticleMain,
    ParticleLoop, ParticleSetLoop, ParticleDuration, ParticleSetDuration,
    ParticlePlay, ParticleStop, UnityAlive,
    BundleLoad, BundleAsset, BundleUnload, ClipLength, ClipHuman, ClipName,
    ControllerGet, ControllerSet, ControllerClips, OverrideCtor, OverrideClip,
    CurrentClipInfos, InfoClip, DestroyOwned,
    PoseTail, PoseComponentTransform, PoseFind, PoseGetLocal, PoseSetLocal, PoseFrameCount, PoseIsChild,
    MeshGroupShow, EffectActionPlay, EffectManualFollow, VisualPerformId, VisualPerformEntity,
    JsspsiTmpAnimator, ComponentGameObject, TransformParent, JsspsiPlayRenderVfx, JsspsiPlayVfx,
    JsspsiVfxTick, JsspsiVfxDelta, JsspsiClothUpdate, JsspsiPostEvent, JsspsiStopPlaying, MethodCount
};
struct Method { BE_MethodDescriptorV1 desc; BE_ResolvedMethodV1 resolved{}; };
Method g_methods[] = {
    {{kGame,kView,kCharacterComp,"StartSpDash","System.Int32","System.Void",1}},
    {{kGame,kView,kCharacterComp,"PreLateTick","System.Single","System.Void",1}},
    {{kGame,kAnimation,kBrain,"ShouldInterruptSpDash","Beyond.Gameplay.View.CharacterAnimationComponent","System.Boolean",1}},
    {{kGame,kView,kCharacterComp,"InterruptSpDashPerform",nullptr,"System.Void",0}},
    {{kGame,kView,kCharacterComp,"ForceStopSpDashPerform",nullptr,"System.Void",0}},
    {{kGame,kView,kCharacterComp,"ForceStopCurrentStatePerform","System.Boolean","System.Void",1}},
    {{kGame,kView,kCharacterComp,"ReleaseInternal","System.Boolean","System.Void",1}},
    {{kGame,kCore,"BaseComponent","get_entity",nullptr,"Beyond.Gameplay.Core.Entity",0}},
    {{kGame,kCore,"Entity","IsValid",nullptr,"System.Boolean",0}},
    {{kGame,kCore,"Entity","get_templateData",nullptr,"Beyond.Gameplay.EntityTemplateData",0}},
    {{kGame,kCore,"Entity","get_charCtrl",nullptr,"Beyond.Gameplay.Core.CharacterController",0}},
    {{kGame,kCore,"CharacterController","get_isMainCharacter",nullptr,"System.Boolean",0}},
    {{kGame,kCore,"Entity","get_movementComponent",nullptr,"Beyond.Gameplay.Core.MovementComponent",0}},
    {{kGame,kCore,"MovementComponent","get_moveGait",nullptr,"Beyond.Gameplay.Core.MoveGait",0}},
    {{kGame,kCore,"MoveGait","get_gait",nullptr,"Beyond.Gameplay.Core.MovementComponent.GroundedMoveGait",0}},
    {{kGame,kView,kComplex,"get_blackboard",nullptr,"Beyond.Gameplay.View.Animation.AnimBlackboard",0}},
    {{kGame,kView,"CharacterAnimationBlackboard","get_isDead",nullptr,"System.Boolean",0}},
    {{kGame,kView,kComplex,"get_animator",nullptr,"UnityEngine.Animator",0}},
    {{kUnity,"UnityEngine","Animator","GetCurrentAnimatorStateInfo","System.Int32","UnityEngine.AnimatorStateInfo",1}},
    {{kUnity,"UnityEngine","Animator","GetNextAnimatorStateInfo","System.Int32","UnityEngine.AnimatorStateInfo",1}},
    {{kUnity,"UnityEngine","Animator","IsInTransition","System.Int32","System.Boolean",1}},
    {{kUnity,"UnityEngine","AnimatorStateInfo","get_shortNameHash",nullptr,"System.Int32",0}},
    {{kUnity,"UnityEngine","AnimatorStateInfo","get_normalizedTime",nullptr,"System.Single",0}},
    {{kUnity,"UnityEngine","AnimatorStateInfo","get_length",nullptr,"System.Single",0}},
    {{kGame,kView,kCharacterComp,"IsPlayingSpDashPerform",nullptr,"System.Boolean",0}},
    {{kGame,kView,"CharacterAnimationComponent.PerformRequest","GetPlayingPerformId",nullptr,"System.String",0}},
    {{kGame,kView,"CharacterAnimationBlackboard","set_isDashing","System.Boolean","System.Void",1}},
    {{kGame,kCore,"MovementComponent","get_moveMode",nullptr,"Beyond.Gameplay.Core.MovementComponent.MoveMode",0}},
    {{kGame,kCore,"MovementComponent","get_isMovingOnGround",nullptr,"System.Boolean",0}},
    {{kGame,kCore,"MovementComponent","get_isInAir",nullptr,"System.Boolean",0}},
    {{kGame,kPerformNs,kHandle,"_TickMainFlow","System.Single","System.Void",1}},
    {{kGame,kPerformNs,kHandle,"Clear","System.Boolean","System.Void",1}},
    {{kGame,kPerformNs,kHandle,"get_curState",nullptr,"Beyond.Gameplay.CharInteractPerformEnums.CharInteractPerformState",0}},
    {{kGame,kPerformNs,kHandle,"_CheckEntityAlive",nullptr,"System.Boolean",0}},
    {{kUnity,"UnityEngine","AnimatorStateInfo","get_loop",nullptr,"System.Boolean",0}},
    {{kGame,kPerformNs,kHandle,"AddInterruptCommand","Beyond.Gameplay.CharInteractPerformEnums.InterruptReason|Beyond.Gameplay.CharInteractPerformEnums.InterruptType|System.Boolean","System.Void",3}},
    {{kUnity,"UnityEngine","Animator","CrossFade","System.Int32|System.Single|System.Int32|System.Single|System.Single|UnityEngine.AnimationBlendStyle|UnityEngine.AnimatorTransitionInterruptionSource|System.Boolean","System.Void",8}},
    {{kGame,kPerformNs,kHandle,"_TryGetCurInterruptCommand","Beyond.Gameplay.Core.CharInteractPerform.InterruptCommandData&","System.Boolean",1}},
    {{kGame,kPerformNs,kHandle,"_DoExit",nullptr,"System.Void",0}},
    {{kGame,kPerformNs,kHandle,"ShowObject","Beyond.Gameplay.Core.CharInteractPerform.ObjectShowActData","System.Void",1}},
    {{kGame,kAnimation,"AnimatorMono","PostAudioEvent","System.UInt32|System.Single","System.Void",2}},
    {{kGame,kAnimation,"AnimatorMono","get_innerAnimatorComponent",nullptr,"Beyond.Gameplay.View.ComplexAnimatorComponent",0}},
    {{kGame,"Beyond.Gameplay.Audio","AudioManager","PostEvent","Beyond.Gameplay.Core.Entity|System.UInt32|Beyond.Audio.AudioCallbackType|Beyond.Audio.AudioEventCallback|System.Object","System.UInt32",5}},
    {{"Audio.Beyond.dll","Beyond.Audio","AudioHashGenerator","Compute","System.String","System.UInt32",1}},
    {{kGame,"Beyond.Gameplay","EffectInstance","get_effectName",nullptr,"System.String",0}},
    {{kGame,"Beyond.Gameplay","EffectInstance","get_effectObj",nullptr,"UnityEngine.GameObject",0}},
    {{kGame,"Beyond.Gameplay","EffectInstance","DurationTick","System.Single","System.Void",1}},
    {{kGame,"Beyond.Gameplay","EffectInstance","Finish","System.Boolean|System.Boolean","System.Void",2}},
    {{kGame,"Beyond.Gameplay","EffectInstance","OnStop",nullptr,"System.Void",0}},
    {{kUnityCore,"UnityEngine","GameObject","GetComponentsInChildren","System.Type|System.Boolean","UnityEngine.Component[]",2}},
    {{"mscorlib.dll","System","Array","get_Length",nullptr,"System.Int32",0}},
    {{"mscorlib.dll","System","Array","GetValue","System.Int32","System.Object",1}},
    {{kParticles,"UnityEngine","ParticleSystem","get_main",nullptr,"UnityEngine.ParticleSystem.MainModule",0}},
    {{kParticles,"UnityEngine","ParticleSystem.MainModule","get_loop",nullptr,"System.Boolean",0}},
    {{kParticles,"UnityEngine","ParticleSystem.MainModule","set_loop","System.Boolean","System.Void",1}},
    {{kParticles,"UnityEngine","ParticleSystem.MainModule","get_duration",nullptr,"System.Single",0}},
    {{kParticles,"UnityEngine","ParticleSystem.MainModule","set_duration","System.Single","System.Void",1}},
    {{kParticles,"UnityEngine","ParticleSystem","Play","System.Boolean","System.Void",1}},
    {{kParticles,"UnityEngine","ParticleSystem","Stop","System.Boolean|UnityEngine.ParticleSystemStopBehavior","System.Void",2}},
    {{kUnityCore,"UnityEngine","Object","op_Implicit","UnityEngine.Object","System.Boolean",1}},
    {{"UnityEngine.AssetBundleModule.dll","UnityEngine","AssetBundle","LoadFromFile","System.String","UnityEngine.AssetBundle",1}},
    {{"UnityEngine.AssetBundleModule.dll","UnityEngine","AssetBundle","LoadAsset","System.Int64|System.Type","UnityEngine.Object",2}},
    {{"UnityEngine.AssetBundleModule.dll","UnityEngine","AssetBundle","Unload","System.Boolean","System.Void",1}},
    {{kUnity,"UnityEngine","AnimationClip","get_length",nullptr,"System.Single",0}},
    {{kUnity,"UnityEngine","AnimationClip","get_humanMotion",nullptr,"System.Boolean",0}},
    {{kUnityCore,"UnityEngine","Object","get_name",nullptr,"System.String",0}},
    {{kUnity,"UnityEngine","Animator","get_runtimeAnimatorController",nullptr,"UnityEngine.RuntimeAnimatorController",0}},
    {{kUnity,"UnityEngine","Animator","set_runtimeAnimatorController","UnityEngine.RuntimeAnimatorController","System.Void",1}},
    {{kUnity,"UnityEngine","RuntimeAnimatorController","get_animationClips",nullptr,"UnityEngine.AnimationClip[]",0}},
    {{kUnity,"UnityEngine","AnimatorOverrideController",".ctor","UnityEngine.RuntimeAnimatorController","System.Void",1}},
    {{kUnity,"UnityEngine","AnimatorOverrideController","SetClip","UnityEngine.AnimationClip|UnityEngine.AnimationClip|System.Boolean","System.Void",3}},
    {{kUnity,"UnityEngine","Animator","GetCurrentAnimatorClipInfo","System.Int32","UnityEngine.AnimatorClipInfo[]",1}},
    {{kUnity,"UnityEngine","AnimatorClipInfo","get_clip",nullptr,"UnityEngine.AnimationClip",0}},
    {{kUnityCore,"UnityEngine","Object","Destroy","UnityEngine.Object","System.Void",1}},
    {{kGame,kView,kCharacterComp,"TailLateTick","System.Single","System.Void",1}},
    {{kUnityCore,"UnityEngine","Component","get_transform",nullptr,"UnityEngine.Transform",0}},
    {{kUnityCore,"UnityEngine","Transform","Find","System.String","UnityEngine.Transform",1}},
    {{kUnityCore,"UnityEngine","Transform","GetLocalPositionAndRotation","UnityEngine.Vector3&|UnityEngine.Quaternion&","System.Void",2}},
    {{kUnityCore,"UnityEngine","Transform","SetLocalPositionAndRotation_Injected","UnityEngine.Vector3&|UnityEngine.Quaternion&","System.Void",2}},
    {{kUnityCore,"UnityEngine","Time","get_frameCount",nullptr,"System.Int32",0}},
    {{kUnityCore,"UnityEngine","Transform","IsChildOf","UnityEngine.Transform","System.Boolean",1}},
    {{kGame,kPerformNs,kHandle,"SetCharMeshGroupShow","Beyond.Gameplay.Core.Entity|Beyond.Gameplay.ModelPartEnum|System.Boolean","System.Void",3}},
    {{kGame,kPerformNs,"EffectPlayAction","OnPlay",nullptr,"System.Void",0}},
    {{kGame,"Beyond.Gameplay","EffectInstance","ManualUpdateFollow",nullptr,"System.Void",0}},
    {{kGame,kPerformNs,kHandle,"get_interactPerformId",nullptr,"System.String",0}},
    {{kGame,kPerformNs,kHandle,"get_entity",nullptr,"Beyond.Gameplay.Core.Entity",0}},
    {{kGame,kPerformNs,"TmpObjectInst","get_animator",nullptr,"UnityEngine.Animator",0}},
    {{kUnityCore,"UnityEngine","Component","get_gameObject",nullptr,"UnityEngine.GameObject",0}},
    {{kUnityCore,"UnityEngine","Transform","get_parent",nullptr,"UnityEngine.Transform",0}},
    {{kGame,"Beyond.Gameplay","EffectInstance","PlayRenderVfx",nullptr,"System.Void",0}},
    {{kGame,kView,"EntityVFX","PlayVFX","System.String","System.Void",1}},
    {{kGame,kView,"EntityVFX","Tick","System.Single","System.Void",1}},
    {{"Rendering.Beyond.dll","Beyond.Rendering","EntityVFXControllerBase","GetDeltaTimeFromLastTick","System.Boolean","System.Single",1}},
    {{"BeyondDynamicBone.dll","BeyondDynamicBone","ClothManager","ClothUpdate",nullptr,"System.Void",0}},
    {{"Audio.Beyond.dll","Beyond.Audio","AudioAdapter","_PostEvent","System.UInt32|System.UInt64|Beyond.Audio.AudioCallbackType|Beyond.Audio.AudioEventCallback|System.Object","System.UInt32",5}},
    {{"Audio.Beyond.dll","Beyond.Audio","AudioAdapter","StopByPlayingId","System.UInt32|System.Int32","System.Void",2}},
};
static_assert(std::size(g_methods) == MethodCount);
struct Field { BE_FieldDescriptorV1 desc; BE_ResolvedFieldV1 resolved{}; };
enum FieldId { TemplateId, Request, RequestHandle, HasCommand,
    CommandType, CommandReason, ActionLogic, ActionIndex, ActionShow, ActionDelay, Effects, VisualActionHandle, VisualActionData, JsspsiTmpObjects, JsspsiRenderHelper, JsspsiHelperVfx, JsspsiVfxOwner, FieldCount };
Field g_fields[] = {
    {{kGame,"Beyond.Gameplay","GameDataWithId","id","System.String"}},
    {{kGame,kView,kCharacterComp,"m_currentRequest","Beyond.Gameplay.View.CharacterAnimationComponent.PerformRequest"}},
    {{kGame,kView,"CharacterAnimationComponent.PerformRequest","handle","Beyond.Gameplay.Core.CharInteractPerform.CharPerformHandleBase"}},
    {{kGame,kPerformNs,kHandle,"m_hasCommand","System.Boolean"}},
    {{kGame,kPerformNs,"InterruptCommandData","interruptType","Beyond.Gameplay.CharInteractPerformEnums.InterruptType"}},
    {{kGame,kPerformNs,"InterruptCommandData","reason","Beyond.Gameplay.CharInteractPerformEnums.InterruptReason"}},
    {{kGame,kPerformNs,"CharInteractPerformActionData","logicId","System.UInt32"}},
    {{kGame,kPerformNs,"ActorActData","actorIndex","System.Int32"}},
    {{kGame,kPerformNs,"ObjectShowActData","show","System.Boolean"}},
    {{kGame,kPerformNs,"CharInteractPerformActionData","delay","System.Single"}},
    {{kGame,kPerformNs,kHandle,"m_effects","System.Collections.Generic.Dictionary<System.Int32,Beyond.ObjectPtr<Beyond.Gameplay.EffectInstance>>"}},
    {{kGame,kPerformNs,"CharInteractPerformActionBase","m_handle","Beyond.Gameplay.Core.CharInteractPerform.CharPerformHandleBase"}},
    {{kGame,kPerformNs,"CharInteractPerformActionBase","m_actionData","Beyond.Gameplay.Core.CharInteractPerform.CharInteractPerformActionData"}},
    {{kGame,kPerformNs,kHandle,"m_tmpObjects",nullptr}},
    {{kGame,kPerformNs,"TmpObjectInst","m_entityRenderHelper",nullptr}},
    {{kGame,kView,"EntityRenderHelper","m_entityVFX",nullptr}},
    {{"Rendering.Beyond.dll","Beyond.Rendering","EntityVFXControllerBase","m_owner",nullptr}},
};
static_assert(std::size(g_fields) == FieldCount);

using StartFn = void(__fastcall*)(void*, int, const void*);
using TickFn = void(__fastcall*)(void*, float, const void*);
// This method is static: RCX is the animation component, not a Brain instance.
using InterruptCheckFn = bool(__fastcall*)(void*, const void*);
using VoidFn = void(__fastcall*)(void*, const void*);
using BoolArgFn = void(__fastcall*)(void*, bool, const void*);
StartFn g_start = nullptr;
TickFn g_pre_late = nullptr;
InterruptCheckFn g_check = nullptr;
VoidFn g_interrupt = nullptr, g_force = nullptr;
BoolArgFn g_force_current = nullptr, g_release = nullptr, g_set_dashing = nullptr;
TickFn g_flow_tick = nullptr;
BoolArgFn g_perform_clear = nullptr;
using CommandFn = void(__fastcall*)(void*, int, int, bool, const void*);
CommandFn g_add_command = nullptr;
using TryInterruptCommandFn = bool(__fastcall*)(void*, void**, const void*);
TryInterruptCommandFn g_try_interrupt_command = nullptr;
using ShowFn = void(__fastcall*)(void*, void*, const void*);
using AudioMonoFn = void(__fastcall*)(void*, uint32_t, float, const void*);
using AudioEntityFn = uint32_t(__fastcall*)(void*, uint32_t, int, void*, void*, const void*);
VoidFn g_do_exit = nullptr;
ShowFn g_show = nullptr;
AudioMonoFn g_audio_mono = nullptr;
AudioEntityFn g_audio_entity = nullptr;
TickFn g_effect_duration = nullptr;
using EffectFinishFn = void(__fastcall*)(void*, bool, bool, const void*);
EffectFinishFn g_effect_finish = nullptr;
VoidFn g_effect_stop = nullptr;
using ObjectClassFn = void*(*)(void*);
using ClassMethodFn = const void*(*)(void*, const char*, int);
using ClassParentFn = void*(*)(void*);
ObjectClassFn g_object_class = nullptr;
ClassMethodFn g_class_method = nullptr;
ClassParentFn g_class_parent = nullptr;
void* g_particle_type = nullptr;
uint32_t g_particle_type_root = 0;
int g_particle_stop_clear = -1;
bool g_ending_splash_skip_contract = false;
bool g_hierarchy_contract = false;
thread_local void* g_main_flow_handle = nullptr;

struct ParticleHold { void* object = nullptr; uint32_t root = 0; bool loop = false; float duration = 0; };
struct EffectHold {
    void* object = nullptr;
    uint32_t root = 0;
    bool prepared = false;
    bool disabled = false;
    uint32_t ticks = 0;
    ParticleHold particles[16]{};
    int count = 0;
};
constexpr int kMaxHeldParticles = 16;
struct Session {
    void* component = nullptr;
    void* handle = nullptr;
    uint32_t root = 0;
    uint32_t handle_root = 0;
    std::shared_ptr<const Configuration> config;
    const CharacterProfile* profile = nullptr;
    DashPolicy policy;
    LoopSchedule loop;
    ImportedLoopSchedule imported_loop;
    bool external_expected = false, external_confirmed = false;
    float external_wait = 0;
    void* entity = nullptr;
    bool deferred_flying_stop = false;
    uint32_t hidden_deferrals = 0;
    uint32_t prop_hide_deferrals = 0;
    uint32_t mesh_group_calls = 0;
    EffectHold effects[kEffectSlotCount]{};
    bool effect_error_logged = false;
    uint32_t visual_teardowns_skipped = 0;
    uint32_t effect_follow_frames = 0;
    void* blackboard = nullptr;
    bool last_requested_dashing = false;
    bool overridden_dashing = false;
    int last_special_hash = 0;
    uint32_t side_changes = 0;
    uint32_t natural_end_deferrals = 0;
    int last_progress_bucket = -1;
    uint32_t animation_replays = 0;
    uint32_t try_exit_calls = 0;
    uint32_t spdash_interrupts_suppressed = 0;
    int last_interrupt_reason = -1;
    int last_interrupt_type = -1;
    bool features_failed = false;
    bool rewound = false;
    uint64_t side_rewind_tick[3]{};
    uint32_t dash_sound_event = 0, dash_sound_playing = 0;
    void* vfx[8]{};
    int vfx_count = 0;
    bool vfx_frozen = false;
    void* vfx_helpers[3]{};
    int vfx_helper_count = 0;
};
// Only game-thread hooks access the session. Configuration callbacks publish an
// immutable snapshot; no Unity/managed calls are made on the settings thread.
Session g_session;
bool g_liino_teardown_contract = true, g_effect_follow_contract = true;
bool g_liino_clean_contract = true;
int g_liino_mesh_groups[2]{-1, -1};
VoidFn g_effect_action_play = nullptr;
const void* g_hash_fields[2]{};
int g_hashes[2]{};
const void* g_locomotion_fields[2]{};
int g_locomotion_hashes[2]{};
int g_sprint = -1, g_dash_mode = -1, g_grounded_mode = -1;
int g_perform_end = -1;
int g_sp_dash_end = -1, g_force_exit = -1;
int g_blend_style = -1, g_blend_interrupt = -1, g_audio_none = -1;
uint32_t g_flying_stop_hashes[kCharacterCount]{};

// Called only by game-thread session consumers. New Jsspsi-only settings must
// not retire a legacy character's dash or its v12 clock. All other fields retain
// the upstream invalidation rule; Jsspsi retains full snapshot invalidation.
bool RefreshSessionConfiguration(const std::shared_ptr<const Configuration>& current) {
    if (!g_session.config || !g_session.profile) return false;
    if (current == g_session.config) return true;
    const auto& profile = *g_session.profile;
    if (profile.seam_file || profile.accessory_file || profile.capture_file) return false;
    auto relevant = *current;
    relevant.jsspsi_capture = g_session.config->jsspsi_capture;
    relevant.jsspsi_seam_fix = g_session.config->jsspsi_seam_fix;
    relevant.jsspsi_accessory_loop = g_session.config->jsspsi_accessory_loop;
    relevant.jsspsi_accessory_mask = g_session.config->jsspsi_accessory_mask;
    relevant.jsspsi_accessory_fade = g_session.config->jsspsi_accessory_fade;
    if (!(relevant == *g_session.config)) return false;
    g_session.config = current;
    return true;
}
void Log(const char* message) {
    if (g_host && g_host->log) g_host->log(g_host->context, kId, message);
}
#include "dash_pose_lease.inl"
#include "dash_feature_guard.inl"
void* Invoke(MethodId id, void* instance, void** args, bool& ok) {
    if (!ok) return nullptr;
    void* exception = nullptr;
    void* result = g_host->runtime_invoke(g_host->context,
        g_methods[id].resolved.method_info, instance, args, &exception);
    if (exception) ok = false;
    return result;
}
void* Object(MethodId id, void* instance, bool& ok) {
    if (!instance) { ok = false; return nullptr; }
    void* result = Invoke(id, instance, nullptr, ok);
    if (!result) ok = false;
    return result;
}
template<class T> T Unbox(void* object, bool& ok) {
    T result{};
    if (!ok || !object) { ok = false; return result; }
    void* value = g_host->object_unbox(g_host->context, object);
    if (!value) { ok = false; return result; }
    std::memcpy(&result, value, sizeof(T));
    return result;
}
template<class T> T Value(MethodId id, void* instance, bool& ok, void** args = nullptr) {
    if (!instance) { ok = false; return {}; }
    return Unbox<T>(Invoke(id, instance, args, ok), ok);
}
void* FieldObject(FieldId id, void* instance) {
    return g_host->field_get_value_object(g_host->context,
        g_fields[id].resolved.field_info, instance);
}
bool StringEquals(void* managed, const char* expected) {
    char text[160]{};
    return managed && g_host->copy_managed_string(g_host->context,
        managed, text, sizeof(text)) > 0 && std::strcmp(text, expected) == 0;
}
// Side-suffixed performs accept the exact profile name or one trailing L/R.
bool PerformMatches(void* managed, const char* expected) {
    char text[160]{};
    if (!managed || g_host->copy_managed_string(g_host->context, managed, text, sizeof(text)) <= 0) return false;
    size_t length = std::strlen(expected);
    if (std::strncmp(text, expected, length) != 0) return false;
    return text[length] == '\0' || ((text[length] == 'L' || text[length] == 'R') && text[length + 1] == '\0');
}
void* OwnedPerform(void* component, const CharacterProfile* profile) {
    bool ok = true;
    if (!profile || !Value<bool>(IsPlayingPerform, component, ok) || !ok) return nullptr;
    void* request = FieldObject(Request, component);
    if (!request) return nullptr;
    void* unboxed = g_host->object_unbox(g_host->context, request);
    if (!unboxed) return nullptr;
    void* id = Invoke(PerformId, unboxed, nullptr, ok);
    return ok && (profile->side_suffixed_perform ? PerformMatches(id, profile->perform_id) : StringEquals(id, profile->perform_id)) ? FieldObject(RequestHandle, request) : nullptr;
}
void* InvokeNamed(void* object, const char* name, int count, void** args, bool& ok, bool value_type = false) {
    if (!ok || !object) { ok = false; return nullptr; }
    void* cls = g_object_class(object);
    const void* method = nullptr;
    for (void* current = cls; current && !method; current = g_class_parent(current))
        method = g_class_method(current, name, count);
    if (!method) { ok = false; return nullptr; }
    void* instance = value_type ? g_host->object_unbox(g_host->context, object) : object;
    if (!instance) { ok = false; return nullptr; }
    void* exception = nullptr;
    void* result = g_host->runtime_invoke(g_host->context, method, instance, args, &exception);
    if (exception) ok = false;
    return result;
}
bool UnityObjectAlive(void* object) {
    if (!object) return false;
    bool ok = true;
    void* args[]{object};
    return Unbox<bool>(Invoke(UnityAlive, nullptr, args, ok), ok) && ok;
}
void* ParticleMainValue(void* particle, bool& ok) {
    void* boxed = Invoke(ParticleMain, particle, nullptr, ok);
    void* result = boxed ? g_host->object_unbox(g_host->context, boxed) : nullptr;
    if (!result) ok = false;
    return result;
}
void StopParticle(void* particle, bool& ok) {
    bool children = false;
    void* args[]{&children, &g_particle_stop_clear};
    Invoke(ParticleStop, particle, args, ok);
}
void RestoreEffect(EffectHold& effect, bool restore_unity) {
    EffectHold old = effect;
    effect = {};
    effect.disabled = true; // Do not reacquire an instance that native code has finished.
    for (int i = 0; i < old.count; ++i) {
        auto& particle = old.particles[i];
        if (restore_unity && UnityObjectAlive(particle.object)) {
            bool ok = true;
            StopParticle(particle.object, ok);
            void* main = ParticleMainValue(particle.object, ok);
            void* loop_args[]{&particle.loop};
            Invoke(ParticleSetLoop, main, loop_args, ok);
            void* duration_args[]{&particle.duration};
            Invoke(ParticleSetDuration, main, duration_args, ok);
        }
        if (particle.root) g_host->gchandle_free(g_host->context, particle.root);
    }
    if (old.root) g_host->gchandle_free(g_host->context, old.root);
}
bool TargetEffectName(void* effect, int index, bool& ok) {
    char name[384]{};
    void* text = Invoke(EffectName, effect, nullptr, ok);
    if (!ok || !text || g_host->copy_managed_string(g_host->context, text, name, sizeof(name)) <= 0) return false;
    char* base = name;
    for (char* c = name; *c; ++c) if (*c == '/' || *c == '\\') base = c + 1;
    if (auto* suffix = std::strrchr(base, '.'); suffix && _stricmp(suffix, ".prefab") == 0) *suffix = 0;
    const char* expected = g_session.profile ? g_session.profile->effects[index] : nullptr;
    return expected && _stricmp(base, expected) == 0;
}
void MaintainEffects() {
    if (!g_session.component || !g_session.policy.active || g_session.features_failed) return;
    const bool by_slot = g_session.profile && g_session.profile->held_effect_actors != 0;
    const uint32_t hold = by_slot ? g_session.profile->held_effect_actors : 0;
    if (!g_session.profile || (!by_slot && !g_session.profile->effects[0])) return;
    for (int slot = 0; slot < (int)std::size(g_session.effects); ++slot) {
        auto& effect = g_session.effects[slot];
        const bool wanted = by_slot ? (hold & (1u << slot)) != 0 : g_session.profile->effects[slot] != nullptr;
        if (!wanted || effect.disabled || effect.prepared) continue;
        bool ok = true;
        if (!effect.object) {
            void* dictionary = FieldObject(Effects, g_session.handle);
            if (!dictionary) continue;
            void* args[]{&slot};
            bool contains = Unbox<bool>(InvokeNamed(dictionary, "ContainsKey", 1, args, ok), ok);
            if (!ok) {
                if (!g_session.effect_error_logged) Log("Sustained dash v9: effect dictionary lookup unavailable.");
                g_session.effect_error_logged = true;
                continue;
            }
            if (!contains) continue; // Initial asynchronous creation can still be pending.
            void* boxed_ptr = InvokeNamed(dictionary, "get_Item", 1, args, ok);
            void* object = InvokeNamed(boxed_ptr, "Lock", 0, nullptr, ok, true);
            if (!ok || !object || (!by_slot && !TargetEffectName(object, slot, ok))) {
                if (!g_session.effect_error_logged) Log("Sustained dash v9: effect pointer/identity not ready; no VFX mutation.");
                g_session.effect_error_logged = true;
                continue;
            }
            effect.object = object;
            effect.root = g_host->gchandle_new(g_host->context, object, 1);
            if (!effect.root) { effect = {}; continue; }
        }
        void* go = Invoke(EffectObject, effect.object, nullptr, ok);
        if (!ok || !UnityObjectAlive(go)) continue; // Still loading: do not mutate an absent instance.
        bool inactive = true;
        void* component_args[]{g_particle_type, &inactive};
        void* array = Invoke(ComponentsInChildren, go, component_args, ok);
        int count = Value<int>(ArrayLength, array, ok);
        if (by_slot && ok && count == 0) {
            // Mesh/trail effects have no particles; only hold the lifetime clock.
            effect.prepared = true;
            if (g_session.config->diagnostics) {
                char message[96];
                std::snprintf(message, sizeof(message), "Sustained dash Jsspsi effects: lifetime prepared actor=%d (no particles).", slot);
                Log(message);
            }
            continue;
        }
        if (!ok || count <= 0 || count > kMaxHeldParticles) {
            if (count == 0 && ok) continue;
            RestoreEffect(effect, true);
            if (!g_session.effect_error_logged) Log("Sustained dash v9: particle enumeration failed; VFX hold skipped.");
            g_session.effect_error_logged = true;
            continue;
        }
        for (int i = 0; i < count && ok; ++i) {
            void* item_args[]{&i};
            void* particle = Invoke(ArrayItem, array, item_args, ok);
            if (!UnityObjectAlive(particle)) { ok = false; break; }
            void* main = ParticleMainValue(particle, ok);
            bool old_loop = Value<bool>(ParticleLoop, main, ok);
            float old_duration = Value<float>(ParticleDuration, main, ok);
            if (!ok || !std::isfinite(old_duration) || old_duration <= 0) { ok = false; break; }
            auto& saved = effect.particles[effect.count];
            saved = {particle, g_host->gchandle_new(g_host->context, particle, 1), old_loop, old_duration};
            if (!saved.root) { ok = false; break; }
            ++effect.count; // Save original state before any instance mutation.
            // The _02 glow is a single 3-second particle in a 5-second system.
            // A plain loop flag would still leave a 2-second gap; overlap renewals.
            if (g_session.profile->renew_short_glow && slot == 1 && std::fabs(old_duration - 5.0f) < .01f) {
                StopParticle(particle, ok);
                float renewal = 2.5f;
                void* duration_args[]{&renewal};
                Invoke(ParticleSetDuration, main, duration_args, ok);
            }
            bool looping = true;
            void* loop_args[]{&looping};
            Invoke(ParticleSetLoop, main, loop_args, ok);
            bool children = false;
            void* play_args[]{&children};
            Invoke(ParticlePlay, particle, play_args, ok);
        }
        if (!ok) {
            RestoreEffect(effect, true);
            Log("Sustained dash v9: particle setup failed; original instance settings restored.");
        } else {
            effect.prepared = true;
            if (g_session.config->diagnostics) {
                char message[144];
                std::snprintf(message, sizeof(message), "Sustained dash v9: effect %d continuous particles prepared: count=%d", slot, effect.count);
                Log(message);
            }
        }
    }
}
// Optional companion contracts and shared Unity helpers use checked boundaries.
#include "dash_calls.inl"
void* g_transform_type = nullptr;
void* TmpObjectAt(int slot, void* dictionary) {
    if (!dictionary) return nullptr;
    bool ok = true;
    void* args[]{&slot};
    const bool contains = DashUnbox<bool>(DashInvokeNamed(dictionary, "ContainsKey", 1, args, ok), ok);
    return ok && contains ? DashInvokeNamed(dictionary, "get_Item", 1, args, ok) : nullptr;
}
void* TmpObjectAnimator(int slot, void* dictionary) {
    bool ok = true;
    void* instance = TmpObjectAt(slot, dictionary);
    void* animator = DashObject(JsspsiTmpAnimator, instance, ok);
    return ok && DashObjectAlive(animator) && DashCallCurrent() ? animator : nullptr;
}
void* TmpObjectAnimator(int slot) {
    return TmpObjectAnimator(slot, OptionalField(JsspsiTmpObjects, g_session.handle));
}
float SeamRewindBlend(int index, float blend);
void TickSideBeasts() {
    const DashSessionSnapshot session;
    DashCallScope calls([&] { return session.Current(); });
    if (!session.Current()) return;
    if (!g_session.profile || !g_session.profile->side_loop) return;
    const LoopConstants c = *g_session.profile->side_loop;
    uint64_t now = GetTickCount64();
    for (int slot = 1; slot <= 2; ++slot) {
        bool ok = true;
        void* animator = TmpObjectAnimator(slot);
        if (!session.Current()) return;
        if (!animator) continue;
        int layer = 0;
        void* layer_args[]{&layer};
        if (DashValue<bool>(InTransition, animator, ok, layer_args) || !ok) continue;
        void* boxed = DashInvoke(CurrentState, animator, layer_args, ok);
        void* state = DashUnboxObject(boxed, ok);
        int hash = DashValue<int>(StateHash, state, ok);
        float time = DashValue<float>(StateTime, state, ok);
        if (!session.Current()) return;
        if (!ok || !hash || time < c.begin || time > c.begin + .2f || now - g_session.side_rewind_tick[slot] < 300) continue;
        float d = SeamRewindBlend(slot + 1, c.blend), o = c.target + (time - c.begin), transition_time = 0;
        bool blend_root_motion = false;
        void* fade_args[]{&hash, &d, &layer, &o, &transition_time, &g_blend_style, &g_blend_interrupt, &blend_root_motion};
        DashInvoke(CrossFade, animator, fade_args, ok);
        if (!session.Current()) return;
        g_session.side_rewind_tick[slot] = now;
    }
}
// Rewind mount 0 to the rider offset; its seam may choose a different blend.
void SyncTmpObjects(float duration, float offset) {
    const DashSessionSnapshot session;
    DashCallScope calls([&] { return session.Current(); });
    if (!session.Current()) return;
    void* animator = TmpObjectAnimator(0); if (!animator) return;
    bool ok = true; int layer = 0; void* layer_args[]{&layer};
    void* boxed = DashInvoke(CurrentState, animator, layer_args, ok);
    void* state = DashUnboxObject(boxed, ok);
    int hash = DashValue<int>(StateHash, state, ok);
    if (!ok || !hash) return;
    float transition_time = 0; bool blend_root_motion = false;
    void* args[]{&hash,&duration,&layer,&offset,&transition_time,&g_blend_style,&g_blend_interrupt,&blend_root_motion};
    DashInvoke(CrossFade,animator,args,ok);
}
bool ReadFrame(void* component, Frame& frame, int& current_hash);
bool DashFeaturesActive() {
    return GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) &&
        !g_stopping && g_session.component && !g_session.features_failed && g_session.profile &&
        (g_session.profile->seam_file || g_session.profile->accessory_file || g_session.profile->freeze_render_vfx ||
         g_session.profile->stop_start_sound || g_session.profile->skipped_effect_actors ||
         g_session.profile->side_loop || g_session.profile->rewind_mount || g_session.profile->capture_file) &&
        g_session.config && g_session.config->enabled && g_config.load() == g_session.config;
}
bool NameHas(const char* name, const char* part) {
    if (!name || !part || !*part) return false;
    for (const char* a = name; *a; ++a) {
        const char* x = a; const char* y = part;
        while (*x && *y && (*x | 0x20) == (*y | 0x20)) { ++x; ++y; }
        if (!*y) return true;
    }
    return false;
}
bool ShouldHold(void* component);
thread_local bool g_render_vfx_origin = false;
using EffectVoidFn = void(__fastcall*)(void*, const void*);
EffectVoidFn g_play_render_vfx = nullptr;
bool OwnedDashEffect(void* effect) {
    char name[200]{}; bool ok = true;
    void* managed = effect ? Invoke(EffectName,effect,nullptr,ok) : nullptr;
    return ok && managed && g_host->copy_managed_string(g_host->context,managed,name,sizeof(name)) > 0 &&
        DashFeaturesActive() && NameHas(name,g_session.profile->owned_name_token);
}
void __fastcall PlayRenderVfxDetour(void* effect, const void* method) {
    bool mine = false;
    if (DashFeaturesActive()) GuardDashFeatures([&] { mine = g_session.profile->freeze_render_vfx && OwnedDashEffect(effect); });
    const bool saved = g_render_vfx_origin;
    struct Restore { bool saved; ~Restore() { g_render_vfx_origin = saved; } } restore{saved};
    g_render_vfx_origin = mine;
    g_play_render_vfx(effect, method);
}
void AddFrozenVfx(void* vfx) {
    if (!vfx) return;
    for (int k = 0; k < g_session.vfx_count; ++k) if (g_session.vfx[k] == vfx) return;
    if (g_session.vfx_count < (int)std::size(g_session.vfx)) g_session.vfx[g_session.vfx_count++] = vfx;
}
using PlayVfxFn = void(__fastcall*)(void*, void*, const void*);
using VfxTickFn = void(__fastcall*)(void*, float, const void*);
PlayVfxFn g_play_vfx = nullptr;
VfxTickFn g_vfx_tick = nullptr;
bool FrozenVfx(void* vfx) {
    for (int k = 0; k < g_session.vfx_count; ++k) if (g_session.vfx[k] == vfx) return true;
    return false;
}
void __fastcall PlayVfxDetour(void* vfx, void* name, const void* method) {
    if (vfx && g_render_vfx_origin && DashFeaturesActive()) GuardDashFeatures([&] { AddFrozenVfx(vfx); });
    g_play_vfx(vfx, name, method);
}
void __fastcall VfxTickDetour(void* vfx, float delta, const void* method) {
    if (DashFeaturesActive()) GuardDashFeatures([&] {
        if (g_session.vfx_frozen && vfx && FrozenVfx(vfx) && g_session.policy.active && ShouldHold(g_session.component)) delta=0;
    });
    g_vfx_tick(vfx,delta,method);
}
// Renderer controllers keep their own Time.time clock, independent of Tick's
// delta. The original still advances its timestamp; owned controllers receive 0.
using VfxDeltaFn = float(__fastcall*)(void*, bool, const void*);
VfxDeltaFn g_vfx_delta = nullptr;
float __fastcall VfxDeltaDetour(void* controller, bool scaled, const void* method) {
    float delta=g_vfx_delta(controller,scaled,method);
    if (DashFeaturesActive()) GuardDashFeatures([&] {
        if (!g_session.vfx_frozen || !controller || g_fields[JsspsiVfxOwner].resolved.offset<=0) return;
        void* owner=*reinterpret_cast<void**>(static_cast<char*>(controller)+g_fields[JsspsiVfxOwner].resolved.offset);
        for (int k=0;owner && k<g_session.vfx_helper_count;++k)
            if (owner==g_session.vfx_helpers[k] && g_session.policy.active && ShouldHold(g_session.component)) { delta=0; return; }
    });
    return delta;
}
void FreezeVfx() {
    const DashSessionSnapshot session;
    DashCallScope calls([&] { return session.Current(); });
    if (!session.Current()) return;
    if (g_session.vfx_frozen || !g_session.config || !g_session.profile->freeze_render_vfx || (!g_vfx_tick && !g_vfx_delta)) return;
    for (int slot = 0; slot < 3 && g_fields[JsspsiRenderHelper].resolved.field_info && g_fields[JsspsiHelperVfx].resolved.field_info; ++slot) {
        void* inst = TmpObjectAt(slot, OptionalField(JsspsiTmpObjects, g_session.handle));
        if (!session.Current()) return;
        void* helper = OptionalField(JsspsiRenderHelper, inst);
        if (!session.Current()) return;
        if (helper && g_session.vfx_helper_count < (int)std::size(g_session.vfx_helpers)) g_session.vfx_helpers[g_session.vfx_helper_count++] = helper;
        void* vfx = OptionalField(JsspsiHelperVfx, helper);
        if (!session.Current()) return;
        AddFrozenVfx(vfx);
    }
    g_session.vfx_frozen = g_session.vfx_count > 0 || g_session.vfx_helper_count > 0;
    if (g_session.config->diagnostics && g_session.vfx_frozen)
        Log("Sustained dash Jsspsi effects: render VFX held after first rewind.");
}
bool OwnedFeaturePerform(void* handle, const CharacterProfile* profile) {
    if (!g_ending_splash_skip_contract || !handle || !profile) return false;
    bool ok=true;
    if (!PerformMatches(Invoke(VisualPerformId,handle,nullptr,ok),profile->perform_id) || !ok) return false;
    void* entity=Object(VisualPerformEntity,handle,ok);
    if (!ok || !Value<bool>(EntityValid,entity,ok)) return false;
    void* control=Object(GetCharCtrl,entity,ok);
    return Value<bool>(IsMain,control,ok) && ok;
}
void* CurrentRequestHandle(void* component) {
    void* request = FieldObject(Request, component);
    return request ? FieldObject(RequestHandle, request) : nullptr;
}
bool CanMove(void* component, const CharacterProfile** matched = nullptr) {
    bool ok = true;
    void* entity = Object(GetEntity, component, ok);
    if (!Value<bool>(EntityValid, entity, ok) || !ok) return false;
    if (matched) {
        void* data = Object(GetTemplate, entity, ok);
        if (!ok) return false;
        void* id = FieldObject(TemplateId, data);
        *matched = nullptr;
        for (const auto& profile : kCharacters)
            if (StringEquals(id, profile.template_id)) { *matched = &profile; break; }
        if (!*matched) return false;
    }
    void* control = Object(GetCharCtrl, entity, ok);
    if (!Value<bool>(IsMain, control, ok) || !ok) return false;
    void* blackboard = Object(GetBlackboard, component, ok);
    if (Value<bool>(IsDead, blackboard, ok) || !ok) return false;
    void* movement = Object(GetMovement, entity, ok);
    int mode = Value<int>(MoveMode, movement, ok);
    bool moving = Value<bool>(Moving, movement, ok);
    bool airborne = Value<bool>(Airborne, movement, ok);
    void* gait = Object(GetGait, movement, ok);
    int value = Value<int>(GaitValue, gait, ok);
    return ok && IsDashOrGroundSprint(mode == g_dash_mode, mode == g_grounded_mode, value == g_sprint, moving, airborne);
}
bool ReadFrame(void* component, Frame& frame, int& current_hash) {
    bool ok = true;
    void* animator = Object(GetAnimator, component, ok);
    if (!ok) return false;
    int layer = 0;
    void* args[]{&layer};
    frame.transitioning = Value<bool>(InTransition, animator, ok, args);
    void* boxed = Invoke(CurrentState, animator, args, ok);
    void* state = boxed ? g_host->object_unbox(g_host->context, boxed) : nullptr;
    current_hash = Value<int>(StateHash, state, ok);
    frame.time = Value<float>(StateTime, state, ok);
    frame.length = Value<float>(StateLength, state, ok);
    // Unity may strip this diagnostic getter. Its absence or invocation failure
    // must not invalidate the state/time/length needed to maintain the dash.
    frame.clip_loop = -1;
    if (ok && g_methods[StateLoop].resolved.method_info) {
        bool loop_ok = true;
        bool loop = Value<bool>(StateLoop, state, loop_ok);
        if (loop_ok) frame.clip_loop = loop ? 1 : 0;
    }
    frame.current_special = current_hash == g_hashes[0] || current_hash == g_hashes[1];
    if (frame.transitioning) {
        boxed = Invoke(NextState, animator, args, ok);
        state = boxed ? g_host->object_unbox(g_host->context, boxed) : nullptr;
        int next = Value<int>(StateHash, state, ok);
        frame.next_hash = next;
        frame.next_time = Value<float>(StateTime, state, ok);
        frame.entering_special = next == g_hashes[0] || next == g_hashes[1];
        frame.outgoing = !frame.entering_special;
        frame.returning_to_locomotion = next == g_locomotion_hashes[0] || next == g_locomotion_hashes[1];
    }
    return ok;
}
// Optional visual frames must stop between managed calls when their owner changes.
bool ReadDashFrame(void* component, Frame& frame, int& current_hash) {
    bool ok = true;
    void* animator = DashObject(GetAnimator, component, ok);
    if (!ok) return false;
    int layer = 0;
    void* args[]{&layer};
    frame.transitioning = DashValue<bool>(InTransition, animator, ok, args);
    void* boxed = DashInvoke(CurrentState, animator, args, ok);
    void* state = DashUnboxObject(boxed, ok);
    current_hash = DashValue<int>(StateHash, state, ok);
    frame.time = DashValue<float>(StateTime, state, ok);
    frame.length = DashValue<float>(StateLength, state, ok);
    // Unity may strip this diagnostic getter. Its absence or invocation failure
    // must not invalidate the state/time/length needed to maintain the dash.
    frame.clip_loop = -1;
    if (ok && g_methods[StateLoop].resolved.method_info) {
        bool loop_ok = true;
        bool loop = DashValue<bool>(StateLoop, state, loop_ok);
        if (loop_ok) frame.clip_loop = loop ? 1 : 0;
    }
    frame.current_special = current_hash == g_hashes[0] || current_hash == g_hashes[1];
    if (frame.transitioning) {
        boxed = DashInvoke(NextState, animator, args, ok);
        state = DashUnboxObject(boxed, ok);
        int next = DashValue<int>(StateHash, state, ok);
        frame.next_hash = next;
        frame.next_time = DashValue<float>(StateTime, state, ok);
        frame.entering_special = next == g_hashes[0] || next == g_hashes[1];
        frame.outgoing = !frame.entering_special;
        frame.returning_to_locomotion = next == g_locomotion_hashes[0] || next == g_locomotion_hashes[1];
    }
    return ok;
}
const char* ClipLoopText(int loop) { return loop < 0 ? "unknown" : loop ? "1" : "0"; }
void RefreshLiinoEffectsAfterPose(void* component);
#include "external_loop.inl"
void CaptureFrame(void* component);
void SeamFixFrame(void* component, bool early = false);
std::atomic_bool g_accessory_live{false};
void AccessoryPrepareFrame(void* component);
void AccessoryApplyFrame(void* component);
void AccessoryFadeFrame(void* component);
bool AccessoryFrameSnapshot(void* component, Frame& frame, int& hash);
std::unordered_set<void*> AccessoryClaimsSnapshot(void* component);
bool AccessoryDefersSeam(void* component);
#include "pose_overlay.inl"
#include "unity_hierarchy.inl"
#include "capture.inl"
#include "seam_fix.inl"
#include "accessory_overlay.inl"
void Cancel(const char* reason, bool stop_perform, bool restore_parameter = true) {
    std::lock_guard<std::recursive_mutex> lock(g_feature_mutex);
    if (!g_session.component) return;
    const bool allow_accessory_fade = !CaptureActive();
    if (g_stopping) DiscardCapture(); else GuardDashFeatures([&] { CaptureEnd(); });
    Session old = std::move(g_session);
    g_session = {};
    // Publish the ended dash before handing its pose lease to the fade owner.
    CancelAccessory(allow_accessory_fade);
    auto fade_owner=AccessoryOwnerSnapshot();
    if (g_dash_pose && (!fade_owner || !fade_owner->fading || fade_owner->lease_context!=g_dash_pose)) g_dash_pose->End();
    ReleaseSeam();
    g_dash_pose.reset();
    if (!g_session.component && g_pose_owner.component == old.component) g_pose_owner.clock.End();
    if (g_external_owner.component == old.component) g_external_owner.pending_restore = true;
    const bool restore_effects = !g_stopping && GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed);
    for (auto& effect : old.effects) RestoreEffect(effect, restore_effects);
    if (old.config && old.config->diagnostics) {
        char text[192];
        std::snprintf(text, sizeof(text), "Sustained dash ended: %s, state_changes=%u, natural_end_deferrals=%u, replays=%u, hide_deferrals=%u", reason, old.side_changes, old.natural_end_deferrals, old.animation_replays, old.hidden_deferrals);
        Log(text);
    }
    // Restore the latest value requested by the game's own blackboard update.
    // Never call into released components or Unity during worker-thread shutdown.
    if (restore_parameter && old.overridden_dashing && old.blackboard && g_set_dashing)
        g_set_dashing(old.blackboard, old.last_requested_dashing, g_methods[SetDashing].resolved.method_info);
    // Clear ownership before invoking the game; stop callbacks may re-enter us.
    if (restore_parameter && old.deferred_flying_stop && old.entity && g_audio_entity) {
        bool ok = true;
        if (Value<bool>(EntityValid, old.entity, ok) && ok)
            g_audio_entity(old.entity, old.profile ? g_flying_stop_hashes[old.profile - kCharacters] : 0,
                g_audio_none, nullptr, nullptr,
                g_methods[AudioEntityPost].resolved.method_info);
    }
    if (stop_perform && CurrentRequestHandle(old.component) == old.handle && g_interrupt)
        g_interrupt(old.component, g_methods[Interrupt].resolved.method_info);
    if (old.root) g_host->gchandle_free(g_host->context, old.root);
    if (old.handle_root) g_host->gchandle_free(g_host->context, old.handle_root);
}
void FailDashFeatures() noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(g_feature_mutex);
        if (g_session.features_failed) return;
        g_session.features_failed=true; g_session.rewound=false; g_session.vfx_frozen=false;
        if (g_dash_pose) g_dash_pose->End();
        auto owner=AccessoryOwnerSnapshot(); if (owner && owner->lease_context) owner->lease_context->End();
        EndAccessory("feature exception"); ReleaseSeam(); g_dash_pose.reset();
        DiscardCapture();
        Log("Sustained dash Jsspsi: features disabled for this dash after an exception.");
    } catch (...) {}
}
bool HoldContext(void* component);
bool ShouldHold(void* component) {
    if (g_stopping || g_session.features_failed || !component || component != g_session.component ||
        !RefreshSessionConfiguration(g_config.load()) || !g_session.config->enabled) return false;
    if (!CanMove(component) || CurrentRequestHandle(component) != g_session.handle) return false;
    return HoldContext(component);
}
// Call the game's zero-delta follow refresh after the final bone writes. It
// respects each effect's mount offset/rotation and does not advance its clock.
bool RefreshEffectFollow(void* effect) {
    bool ok = true;
    void* go = Invoke(EffectObject, effect, nullptr, ok);
    if (!ok || !UnityObjectAlive(go)) return false;
    Invoke(EffectManualFollow, effect, nullptr, ok);
    return ok;
}
void RefreshLiinoEffectsAfterPose(void* component) {
    if (!g_effect_follow_contract || g_session.profile != &kCharacters[1] ||
        component != g_session.component || !ShouldHold(component)) return;
    void* handle = g_session.handle;
    void* dictionary = FieldObject(Effects, handle);
    if (!dictionary) return;
    unsigned refreshed = 0;
    // Include the short entry accents as well as the sustained particles.
    // Re-lock ObjectPtr each frame: finished/recycled effects must not be kept.
    for (int slot = 0; slot < static_cast<int>(kEffectSlotCount); ++slot) {
        if (g_session.handle != handle || g_session.component != component) return;
        bool ok = true; void* args[]{&slot};
        if (!Unbox<bool>(InvokeNamed(dictionary, "ContainsKey", 1, args, ok), ok)) continue;
        void* boxed = InvokeNamed(dictionary, "get_Item", 1, args, ok);
        void* effect = InvokeNamed(boxed, "Lock", 0, nullptr, ok, true);
        if (!ok || !effect) continue;
        uint32_t pin = g_host->gchandle_new(g_host->context, effect, 1);
        if (!pin) continue;
        char name[384]{};
        void* text = Invoke(EffectName, effect, nullptr, ok);
        bool named = ok && text && g_host->copy_managed_string(g_host->context, text, name, sizeof(name)) > 0;
        const char* base = name;
        for (const char* c = name; *c; ++c) if (*c == '/' || *c == '\\') base = c + 1;
        constexpr char prefix[] = "P_fxbat_liino_sprint_dash_sp_";
        if (named && g_session.handle == handle && g_session.component == component &&
            _strnicmp(base, prefix, sizeof(prefix) - 1) == 0 && RefreshEffectFollow(effect)) ++refreshed;
        g_host->gchandle_free(g_host->context, pin);
    }
    if (g_session.handle == handle && refreshed && ++g_session.effect_follow_frames == 1 && g_session.config->diagnostics)
        Log("Sustained dash Liino: effect mounts refreshed after final bone pose; native offsets retained.");
}
// Identify the native perform directly, including actions executed inside
// StartSpDash before our animation session is armed, and its exit actions.
// Never borrow the current session's identity for another handle.
bool CleanLiinoPerform(void* handle, void* target = nullptr) {
    if (!handle || g_stopping || !g_liino_clean_contract ||
        GetCurrentThreadId() != g_game_thread.load(std::memory_order_relaxed)) return false;
    auto config = g_config.load();
    if (!config->enabled || !config->liino_clean || !(config->characters & (1u << 1))) return false;
    bool ok = true;
    if (!StringEquals(Invoke(VisualPerformId, handle, nullptr, ok), kCharacters[1].perform_id) || !ok) return false;
    void* entity = Object(VisualPerformEntity, handle, ok);
    if (!ok || (target && target != entity) || !Value<bool>(EntityValid, entity, ok)) return false;
    void* control = Object(GetCharCtrl, entity, ok);
    return Value<bool>(IsMain, control, ok) && ok;
}
void __fastcall EffectActionPlayDetour(void* action, const void* method) {
    if (action && !g_stopping && g_ending_splash_skip_contract &&
        GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed)) {
        try {
            const auto config = g_config.load();
            uint32_t skipped_actors = 0;
            for (size_t i = 0; i < kCharacterCount; ++i)
                if (config->characters & (1u << i)) skipped_actors |= kCharacters[i].skipped_effect_actors;
            if (config->enabled && !config->jsspsi_capture && skipped_actors) {
                // Check the actor fields before any managed identity getter.
                void* data = FieldObject(VisualActionData, action);
                bool ok = data != nullptr;
                const int actor = ok ? Unbox<int>(FieldObject(ActionIndex, data), ok) : -1;
                if (ok && actor >= 0 && actor < 32) {
                    for (size_t i = 0; i < kCharacterCount; ++i) {
                        const auto& profile = kCharacters[i];
                        if (!(config->characters & (1u << i)) || !(profile.skipped_effect_actors & (1u << actor))) continue;
                        if (OwnedFeaturePerform(FieldObject(VisualActionHandle, action), &profile) &&
                            !g_stopping && g_config.load() == config) return;
                    }
                }
            }
        } catch (...) {
            Log("Sustained dash Jsspsi effects: splash identity failed; native action retained.");
        }
    }
    // All EffectPlayActions belonging to this exact perform are cosmetic:
    // entry/exit particles, continuous trails, and material dissolve/glow VFX.
    if (action && !g_stopping && g_liino_clean_contract &&
        GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) &&
        CleanLiinoPerform(FieldObject(VisualActionHandle, action))) return;
    if (action && GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) &&
        g_session.profile == &kCharacters[1] && g_session.policy.active &&
        FieldObject(VisualActionHandle, action) == g_session.handle && ShouldHold(g_session.component)) {
        void* data = FieldObject(VisualActionData, action);
        if (!data) { g_effect_action_play(action, method); return; }
        bool ok = true;
        unsigned logic = Unbox<unsigned>(FieldObject(ActionLogic, data), ok);
        int actor = Unbox<int>(FieldObject(ActionIndex, data), ok);
        float delay = Unbox<float>(FieldObject(ActionDelay, data), ok);
        if (ok && IsLiinoFlightTeardown(logic, actor, delay)) {
            ++g_session.visual_teardowns_skipped;
            if (g_session.config->diagnostics) {
                char text[160];
                std::snprintf(text, sizeof(text), "Sustained dash Liino: skipped flight teardown effect actor=%d logic=%u delay=%.3f.", actor, logic, delay);
                Log(text);
            }
            return;
        }
    }
    g_effect_action_play(action, method);
}
// IsPlayingSpDashPerform can be cleared before the queued SpDashEnd command
// reaches _TryExit. The pinned handle remains the ownership proof for the
// two exit hooks, so this context deliberately omits that transient predicate.
bool HoldContext(void* component) {
    if (g_stopping || g_session.features_failed || !component || component != g_session.component ||
        !RefreshSessionConfiguration(g_config.load()) || !g_session.config->enabled ||
        !CanMove(component)) return false;
    // Keep the natural entry alive until the Animator sees Dash's CrossFade.
    if (g_session.policy.WithinEntryWindow()) return true;
    Frame frame;
    int hash = 0;
    return ReadFrame(component, frame, hash) &&
        (frame.current_special || frame.entering_special) &&
        !frame.outgoing;
}
void __fastcall EffectDurationDetour(void* instance, float delta, const void* method) {
    if (GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed)) {
        for (auto& effect : g_session.effects) {
            if (effect.object == instance && effect.prepared &&
                (!g_session.profile || !g_session.profile->held_effect_actors || g_session.rewound) &&
                ShouldHold(g_session.component)) {
                if (++effect.ticks == 1 && g_session.config->diagnostics)
                    Log("Sustained dash v9: held target effect lifetime/auto-fade clock; particles keep simulating.");
                delta = 0; // Only the effect lifetime clock, not ParticleSystem/FollowTick.
                break;
            }
        }
    }
    g_effect_duration(instance, delta, method);
}
void RestoreFinishedEffect(void* instance) {
    if (GetCurrentThreadId() != g_game_thread.load(std::memory_order_relaxed)) return;
    for (auto& effect : g_session.effects)
        if (effect.object == instance) RestoreEffect(effect, true);
}
void __fastcall EffectFinishDetour(void* instance, bool force, bool func, const void* method) {
    RestoreFinishedEffect(instance);
    g_effect_finish(instance, force, func, method);
}
void __fastcall EffectStopDetour(void* instance, const void* method) {
    RestoreFinishedEffect(instance);
    g_effect_stop(instance, method);
}
bool ReadMovementState(void* component, int& mode, int& gait, bool& moving, bool& airborne) {
    bool ok = true;
    void* entity = Object(GetEntity, component, ok);
    if (!Value<bool>(EntityValid, entity, ok) || !ok) return false;
    void* movement = Object(GetMovement, entity, ok);
    mode = Value<int>(MoveMode, movement, ok);
    moving = Value<bool>(Moving, movement, ok);
    airborne = Value<bool>(Airborne, movement, ok);
    void* gait_object = Object(GetGait, movement, ok);
    gait = Value<int>(GaitValue, gait_object, ok);
    return ok;
}
bool __fastcall TryInterruptCommandDetour(void* handle, void** command_out, const void* method) {
    const bool result = g_try_interrupt_command(handle, command_out, method);
    void* command = result && command_out ? *command_out : nullptr;
    if (command && handle == g_session.handle && g_session.component) {
        ++g_session.try_exit_calls;
        bool ok = true;
        int type = command ? Unbox<int>(FieldObject(CommandType, command), ok) : -1;
        int reason = command ? Unbox<int>(FieldObject(CommandReason, command), ok) : -1;
        int mode = -1, gait = -1;
        bool moving = false, airborne = false;
        bool movement_ok = ReadMovementState(g_session.component, mode, gait, moving, airborne);
        bool owned = OwnedPerform(g_session.component, g_session.profile) == g_session.handle;
        bool hold = ok && movement_ok && HoldContext(g_session.component);
        if (g_session.config->diagnostics &&
            (g_session.try_exit_calls == 1 || (command && (type != g_session.last_interrupt_type || reason != g_session.last_interrupt_reason)))) {
            g_session.last_interrupt_type = type;
            g_session.last_interrupt_reason = reason;
            char text[256];
            std::snprintf(text, sizeof(text), "Sustained dash Perform command: command=%p, reason=%d, type=%d, suppress=%d, hold=%d, mode=%d, gait=%d, moving=%d, airborne=%d, owned=%d",
                command, reason, type, hold && reason == g_sp_dash_end && type == g_force_exit,
                hold, mode, gait, moving, airborne, owned);
            Log(text);
        }
        // The game's special-dash end command is the expected transition from
        // dash to sprint. Let every other explicit exit retain its semantics.
        // 1.6.3 inlines _TryExit into _TickMainFlow. Filter its input at the
        // surviving command dequeue instead of hooking the unused wrapper.
        if (hold && reason == g_sp_dash_end && type == g_force_exit) {
            *command_out = nullptr;
            return false;
        }
    }
    return result;
}
void __fastcall DashingDetour(void* blackboard, bool value, const void* method) {
    if (blackboard == g_session.blackboard && g_session.component) {
        g_session.last_requested_dashing = value;
        if (!value && ShouldHold(g_session.component)) {
            if (!g_session.overridden_dashing && g_session.config->diagnostics)
                Log("Sustained dash: preserving native isDashing across Dash -> grounded Sprint.");
            g_session.overridden_dashing = true;
            value = true;
        }
    }
    g_set_dashing(blackboard, value, method);
}

struct FlowScope {
    void* previous;
    explicit FlowScope(void* handle) : previous(g_main_flow_handle) { g_main_flow_handle = handle; }
    ~FlowScope() { g_main_flow_handle = previous; }
};
void __fastcall FlowDetour(void* handle, float delta, const void* method) {
    FlowScope scope(handle);
    g_flow_tick(handle, delta, method);
}
// 1.6.3 inlines _CheckTrackEnd, but natural cleanup still reaches _DoExit.
// End itself must run: the resource creates props/effects on entering it.
void __fastcall DoExitDetour(void* handle, const void* method) {
    if (handle != g_session.handle || !g_session.component) {
        g_do_exit(handle, method);
        return;
    }
    bool ok = true;
    bool command = Unbox<bool>(FieldObject(HasCommand, handle), ok);
    int phase = Value<int>(PerformState, handle, ok);
    bool alive = Value<bool>(HandleAlive, handle, ok);
    bool defer = ok && CanDeferNaturalEnd(true, g_main_flow_handle == handle,
        g_session.policy.active, ShouldHold(g_session.component), command, alive, phase == g_perform_end);
    if (defer) {
        if (++g_session.natural_end_deferrals == 1 && g_session.config->diagnostics)
            Log("Sustained dash v8: deferred owned Perform End track cleanup.");
        return;
    }
    g_do_exit(handle, method);
}
// Optional: absent on builds where the mesh-group action is unavailable.
bool g_mesh_group_contract = true;
using MeshGroupFn = void(__fastcall*)(void*, void*, int, bool, const void*);
MeshGroupFn g_mesh_group_show = nullptr;
void __fastcall MeshGroupShowDetour(void* handle, void* entity, int group, bool show, const void* method) {
    if (entity && (group == g_liino_mesh_groups[0] || group == g_liino_mesh_groups[1]) &&
        CleanLiinoPerform(handle, entity)) {
        // Skip both edges. SetCharMeshGroupShow caches !show on the first call;
        // substituting false would incorrectly restore true during Clear.
        if (g_config.load()->diagnostics) {
            char text[112];
            std::snprintf(text, sizeof(text), "Liino clean dash: omitted mesh group %d show=%d; native restore cache untouched.", group, show ? 1 : 0);
            Log(text);
        }
        return;
    }
    bool owned = GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) &&
        g_session.component && handle == g_session.handle && entity == g_session.entity;
    bool defer = owned && !show && g_session.profile && g_session.profile->defer_mesh_group_hide &&
        (group == g_liino_mesh_groups[0] || group == g_liino_mesh_groups[1]) &&
        g_session.policy.active && ShouldHold(g_session.component);
    // Model parts are what the player sees appear and disappear, so record every
    // toggle this perform makes rather than only the first deferral.
    if (owned && g_session.config->diagnostics && g_session.mesh_group_calls < 12) {
        char text[160];
        std::snprintf(text, sizeof(text), "Sustained dash: mesh group %d show=%d deferred=%d, call=%u",
            group, show ? 1 : 0, defer ? 1 : 0, ++g_session.mesh_group_calls);
        Log(text);
    }
    if (defer) { ++g_session.hidden_deferrals; return; }
    g_mesh_group_show(handle, entity, group, show, method);
}
void __fastcall ShowObjectDetour(void* handle, void* action, const void* method) {
    if (action && handle == g_session.handle && g_session.policy.active && ShouldHold(g_session.component)) {
        bool ok = true;
        auto logic = Unbox<uint32_t>(FieldObject(ActionLogic, action), ok);
        int index = Unbox<int>(FieldObject(ActionIndex, action), ok);
        bool show = Unbox<bool>(FieldObject(ActionShow, action), ok);
        float delay = Unbox<float>(FieldObject(ActionDelay, action), ok);
        if (ok && g_session.profile && IsTargetHide(g_session.profile->prop_hide, logic, index, show, delay)) {
            if (++g_session.prop_hide_deferrals == 1 && g_session.config->diagnostics)
                Log("Sustained dash v8: deferred prop hide; native Clear still owns cleanup.");
            return;
        }
    }
    g_show(handle, action, method);
}
uint32_t SessionFlyingStopHash() {
    return g_session.profile ? g_flying_stop_hashes[g_session.profile - kCharacters] : 0;
}
bool DeferFlyingStop(uint32_t hash) {
    if (!hash || hash != SessionFlyingStopHash() || !g_session.policy.active || !ShouldHold(g_session.component)) return false;
    if (!g_session.deferred_flying_stop && g_session.config->diagnostics)
        Log("Sustained dash v8: deferred Flying_Stop until session exit.");
    g_session.deferred_flying_stop = true;
    return true;
}
// Scope the dash-start audio capture to this character or its companions.
thread_local bool g_audio_origin = false;
struct AudioOriginScope {
    bool saved;
    explicit AudioOriginScope(bool origin) : saved(g_audio_origin) { g_audio_origin = origin; }
    ~AudioOriginScope() { g_audio_origin = saved; }
};
bool AnimAudioOrigin(void* mono) {
    if (!mono || !DashFeaturesActive() || !g_session.profile->stop_start_sound || !g_methods[ClipName].resolved.method_info) return false;
    char name[120] = "";
    bool ok = true;
    void* managed = Invoke(ClipName, mono, nullptr, ok);
    if (ok && managed) g_host->copy_managed_string(g_host->context, managed, name, sizeof(name));
    return NameHas(name,g_session.profile->owned_name_token) || NameHas(name,g_session.profile->companion_name_filter);
}
using PostEventFn = uint32_t(__fastcall*)(uint32_t, uint64_t, int, void*, void*, const void*);
PostEventFn g_post_event = nullptr;
uint32_t __fastcall PostEventDetour(uint32_t id, uint64_t go, int type, void* callback, void* cookie, const void* method) {
    const uint32_t playing = g_post_event(id, go, type, callback, cookie, method);
    if (g_audio_origin && DashFeaturesActive() && g_session.profile->stop_start_sound) GuardDashFeatures([&] {
        if ( !g_session.dash_sound_playing && !g_session.rewound && playing) {
            g_session.dash_sound_event = id;
            g_session.dash_sound_playing = playing;
        }
    });
    return playing;
}
void StopDashSound() {
    const DashSessionSnapshot session;
    if (!session.Current()) return;
    if (!g_session.config || !g_session.profile->stop_start_sound || !g_session.dash_sound_playing || !g_methods[JsspsiStopPlaying].resolved.method_info) return;
    uint32_t playing = g_session.dash_sound_playing;
    int fade = kJsspsiSoundFadeMs;
    void* args[]{&playing, &fade};
    void* exception = nullptr;
    g_host->runtime_invoke(g_host->context, g_methods[JsspsiStopPlaying].resolved.method_info, nullptr, args, &exception);
    if (!session.Current()) return;
    if (session.config->diagnostics) {
        char text[160];
        std::snprintf(text, sizeof(text), "Sustained dash Jsspsi audio: dash sound stopped: event=%u playing=%u%s", g_session.dash_sound_event, playing, exception ? " (exception)" : "");
        Log(text);
    }
    g_session.dash_sound_playing = 0;
}
void __fastcall AudioMonoDetour(void* mono, uint32_t hash, float clip_in, const void* method) {
    bool origin = false;
    if (DashFeaturesActive()) GuardDashFeatures([&] { origin = AnimAudioOrigin(mono); });
    AudioOriginScope scope(origin);
    if (GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) &&
        hash == SessionFlyingStopHash() && g_session.component) {
        bool ok = true;
        void* component = Object(AudioMonoComponent, mono, ok);
        if (ok && component == g_session.component && DeferFlyingStop(hash)) return;
    }
    g_audio_mono(mono, hash, clip_in, method);
}
uint32_t __fastcall AudioEntityDetour(void* entity, uint32_t hash, int callback_type,
    void* callback, void* cookie, const void* method) {
    const bool origin = DashFeaturesActive() && g_session.profile->stop_start_sound && entity == g_session.entity;
    AudioOriginScope scope(origin);
    if (GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) &&
        entity == g_session.entity && !callback && !cookie && callback_type == g_audio_none && DeferFlyingStop(hash))
        return 0; // No playing instance or callback was requested for this deferred stop.
    return g_audio_entity(entity, hash, callback_type, callback, cookie, method);
}
void __fastcall PerformClearDetour(void* handle, bool releasing, const void* method) {
    if (handle == g_session.handle && g_session.component) {
        if (g_session.config->diagnostics) {
            void* stack[12]{};
            USHORT count = CaptureStackBackTrace(0, 12, stack, nullptr);
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
            char text[384] = "Sustained dash Clear stack (GameAssembly-relative):";
            size_t used = std::strlen(text);
            for (USHORT i = 0; i < count && used < sizeof(text) - 24; ++i) {
                auto address = reinterpret_cast<uintptr_t>(stack[i]);
                if (address >= base && address - base < 0x20000000)
                    used += std::snprintf(text + used, sizeof(text) - used, " %llx", static_cast<unsigned long long>(address - base));
            }
            Log(text);
        }
        Cancel(releasing ? "perform released" : "perform cleared", false, !releasing);
    }
    g_perform_clear(handle, releasing, method);
}
void __fastcall CommandDetour(void* handle, int reason, int type, bool weak, const void* method) {
    if (handle == g_session.handle && g_session.component && g_session.config->diagnostics) {
        char text[144];
        std::snprintf(text, sizeof(text), "Sustained dash Perform interrupt command: reason=%d, type=%d, weak=%d", reason, type, weak);
        Log(text);
    }
    if (handle == g_session.handle && reason == g_sp_dash_end && type == g_force_exit &&
        ShouldHold(g_session.component)) return;
    if (handle == g_session.handle && !(reason == g_sp_dash_end && type == g_force_exit))
        Cancel("explicit perform command", false);
    // Explicit interrupt commands always keep their original behavior.
    g_add_command(handle, reason, type, weak, method);
}
void __fastcall StartDetour(void* component, int index, const void* method) {
    SuspendDashCalls suspended_calls;
    g_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    std::lock_guard<std::recursive_mutex> feature_lock(g_feature_mutex);
    bool main_character=false;
    if (g_accessory_live.load(std::memory_order_relaxed)) GuardDashFeatures([&] {
        bool ok=true; void* entity=Object(GetEntity,component,ok);
        void* control=ok && Value<bool>(EntityValid,entity,ok) ? Object(GetCharCtrl,entity,ok) : nullptr;
        main_character=Value<bool>(IsMain,control,ok) && ok;
    });
    {
        // Native Start can cancel the preceding dash reentrantly. Its Cancel
        // must not start a new fade, even if this new perform is not handled here.
        AccessoryStartScope accessory_start(main_character);
        g_start(component, index, method);
    }
    auto config = g_config.load();
    if (g_stopping || !config->enabled || !component || index < 0) return;
    const CharacterProfile* profile = nullptr;
    if (!CanMove(component, &profile)) return;
    if (!(config->characters & (1u << (profile - kCharacters)))) return;
    bool ok = true;
    // This class is already initialized by the original StartSpDash. Read the
    // game's real state identifiers, not names guessed from documentation.
    for (int side = 0; side < 2; ++side)
        g_hashes[side] = Unbox<int>(g_host->field_get_value_object(g_host->context, g_hash_fields[side], nullptr), ok);
    for (int i = 0; i < 2; ++i)
        g_locomotion_hashes[i] = Unbox<int>(g_host->field_get_value_object(g_host->context, g_locomotion_fields[i], nullptr), ok);
    if (!ok || !g_hashes[0] || !g_hashes[1] || g_hashes[0] == g_hashes[1]) return;
    void* blackboard = Object(GetBlackboard, component, ok);
    if (!ok) return;
    void* handle = OwnedPerform(component, profile);
    if (!handle) return;
    // Repeated calls for the same perform are not a fresh animation session.
    if (g_session.component == component && g_session.handle == handle) return;
    EndAccessory();
    Cancel("new special dash", true);
    // A failed post-dash fade may have flagged an otherwise idle session.
    g_session = {};
    g_session.component = component;
    g_session.handle = handle;
    g_session.config = config;
    g_session.profile = profile;
    g_session.loop.c = profile->loop;
    g_session.blackboard = blackboard;
    g_session.entity = Object(GetEntity, component, ok);
    if (!ok) { Cancel("entity unavailable", false); return; }
    g_session.root = g_host->gchandle_new(g_host->context, component, 1);
    g_session.handle_root = g_host->gchandle_new(g_host->context, handle, 1);
    if (!g_session.root || !g_session.handle_root) Cancel("root failed", false);
    else {
        if (profile->seam_file || profile->accessory_file) GuardDashFeatures([&] {
            auto lease_context=std::make_shared<DashPoseContext>(); lease_context->component=component; lease_context->handle=handle;
            lease_context->diagnostics=config->diagnostics; g_dash_pose=std::move(lease_context);
        });
        if (config->external_loop) {
            BeginPoseOverlay(component, profile);
        }
        if (!g_session.external_expected && g_external_owner.component == component) {
            Cancel("external controller rollback pending", true); return;
        }
        if (config->jsspsi_capture && profile->capture_file) {
            g_session.loop.c.begin = kCaptureNoRewind; // Preserve native animation throughout this captured dash.
            GuardDashFeatures([&] { CaptureBegin(component, profile); });
        }
        if (profile->accessory_file && config->jsspsi_accessory_loop && !config->jsspsi_capture) GuardDashFeatures([&] { BeginAccessory(component); });
        if (config->diagnostics) {
            char text[160];
            std::snprintf(text, sizeof(text), "Sustained dash armed for %s: left_hash=%d, right_hash=%d", profile->codename, g_hashes[0], g_hashes[1]);
            Log(text);
        }
    }
}
void __fastcall PreLateDetour(void* component, float delta, const void* method) {
    ExternalRestoreWhenIdle(component);
    if (g_session.component) {
        if (g_session.features_failed) Cancel("optional feature exception", true);
        else if (g_stopping || !RefreshSessionConfiguration(g_config.load()))
            Cancel("configuration changed", true);
        else if (!CanMove(g_session.component))
            Cancel("movement or owner ended", true);
        else if (g_session.component == component) {
            const DashSessionSnapshot observed_session;
            Frame frame;
            frame.delta = delta;
            frame.allowed = CurrentRequestHandle(component) == g_session.handle;
            int hash = 0;
            if (!ReadFrame(component, frame, hash)) frame.allowed = false;
            if (g_session.external_expected) {
                if (!ExternalControllerOwned(component)) frame.allowed = false;
                else if (!g_session.external_confirmed && frame.current_special && !frame.transitioning) {
                    if (ExternalClipPlaying(component, hash)) {
                        g_session.external_confirmed = true;
                        Log("Sustained dash v11: external return clip confirmed active; using baked closed-loop window.");
                    } else if ((g_session.external_wait += delta) > .35f) {
                        RestoreExternal(true);
                        g_session.external_expected = false;
                        if (g_external_owner.component == component) frame.allowed = false;
                        else Log("Sustained dash v11: imported clip not observed; original controller restored, using v9 loop.");
                    }
                }
            }
            if (g_session.last_special_hash && frame.current_special && hash != g_session.last_special_hash)
                frame.allowed = false; // The native selected side is fixed for this session.
            auto loop_action = g_session.external_confirmed ?
                g_session.imported_loop.Update(g_session.policy.active, frame) :
                g_session.loop.Update(g_session.policy.active, frame);
            if (loop_action == LoopAction::Abort) {
                if (g_session.config->diagnostics) {
                    char detail[192];
                    std::snprintf(detail, sizeof(detail), "Sustained dash %s loop rejected: current=%.3f next=%.3f transition=%d expected_target=%.3f wait=%.3f",
                        g_session.external_confirmed ? "v11 imported" : "v9",
                        frame.time, frame.next_time, frame.transitioning,
                        g_session.external_confirmed ? ImportedLoopSchedule::TargetAt(g_session.imported_loop.submitted_time) : g_session.loop.TargetAt(g_session.loop.submitted_time),
                        g_session.external_confirmed ? g_session.imported_loop.waiting : g_session.loop.waiting);
                    Log(detail);
                }
                Cancel("loop interrupted, late, or not acknowledged", true);
            } else if (loop_action == LoopAction::Blend) {
                bool ok = true;
                int layer = 0;
                float duration = g_session.external_confirmed ? 0.0f : SeamRewindBlend(0, g_session.loop.c.blend);
                const float beast_duration = SeamRewindBlend(1, g_session.loop.c.blend);
                float offset = g_session.external_confirmed ? ImportedLoopSchedule::TargetAt(frame.time) : g_session.loop.TargetAt(frame.time);
                float transition_time = 0;
                bool blend_root_motion = false;
                void* animator = Object(GetAnimator, component, ok);
                if (!observed_session.Current()) {
                    g_pre_late(component, delta, method);
                    return;
                }
                void* args[]{&hash, &duration, &layer, &offset, &transition_time, &g_blend_style, &g_blend_interrupt, &blend_root_motion};
                if (g_session.external_confirmed) g_session.imported_loop.Submitted(frame.time);
                else g_session.loop.Submitted(frame.time);
                Invoke(CrossFade, animator, args, ok);
                if (!observed_session.Current()) {
                    // Managed playback may synchronously deliver a native exit.
                } else if (ok) {
                    ++g_session.animation_replays;
                    g_session.rewound = true;
                    if (DashFeaturesActive()) GuardDashFeatures([&] {
                        DashCallScope calls([&] { return observed_session.Current(); });
                        StopDashSound();
                        if (!observed_session.Current()) return;
                        FreezeVfx();
                        if (!observed_session.Current()) return;
                        if (g_session.profile->rewind_mount) SyncTmpObjects(beast_duration,offset);
                    });
                    if (!observed_session.Current()) {
                        g_pre_late(component, delta, method);
                        return; // No diagnostics or policy updates from the retired frame.
                    }
                    if (g_session.config->diagnostics && (g_session.animation_replays <= 4 || g_session.animation_replays % 16 == 0)) {
                        char text[240];
                        std::snprintf(text, sizeof(text), "Sustained dash %s: hash=%d, source=%.3f, target=%.3f, duration=%.3f, count=%u",
                            g_session.external_confirmed ? "v11 imported loop wrap" : "v9 normalized bob blend", hash, frame.time, offset, duration, g_session.animation_replays);
                        Log(text);
                    }
                    // Observe the actual transition/rewind on following ticks;
                    // never fabricate a successful seek or reset frame.time.
                } else Cancel("loop blend invocation failed", true);
            }
            if (!observed_session.Current()) {
                g_pre_late(component, delta, method);
                return; // A synchronous callback started a new session; discard the old frame.
            }
            if (DashFeaturesActive() && g_session.policy.active && !g_session.config->jsspsi_capture) GuardDashFeatures([&] {
                if (g_session.profile->side_loop) TickSideBeasts();
            });
            if (!observed_session.Current()) {
                g_pre_late(component, delta, method);
                return; // Side-beast playback can synchronously cancel or replace this dash too.
            }
            Decision decision = g_session.policy.Update(frame);
            if (decision == Decision::Keep) MaintainEffects();
            if (decision == Decision::Cancel) {
                if (g_session.config->diagnostics) {
                    char text[280];
                    std::snprintf(text, sizeof(text),
                        "Sustained dash exit: hash=%d, next=%d, time=%.3f, length=%.3f, clip_loop=%s, perform=%d, special=%d, entering=%d, outgoing=%d, pending=%.3f",
                        hash, frame.next_hash, frame.time, frame.length, ClipLoopText(frame.clip_loop), frame.allowed, frame.current_special, frame.entering_special,
                        frame.outgoing, g_session.policy.pending_time);
                    Log(text);
                }
                Cancel("native state or perform ended", true);
            } else if (g_session.component && frame.current_special && hash != g_session.last_special_hash) {
                if (g_session.last_special_hash) ++g_session.side_changes;
                g_session.last_special_hash = hash;
                if (g_session.config->diagnostics && (g_session.side_changes < 5 || g_session.side_changes % 16 == 0)) {
                    char text[160];
                    std::snprintf(text, sizeof(text), "Sustained dash selected state=%s, time=%.3f, length=%.3f, clip_loop=%s",
                        hash == g_hashes[0] ? "left" : "right", frame.time, frame.length, ClipLoopText(frame.clip_loop));
                    Log(text);
                }
            }
            if (g_session.component && frame.current_special && std::isfinite(frame.time)) {
                int bucket = static_cast<int>(std::min(100.0f, std::max(0.0f, frame.time)) * 10.0f);
                if (bucket != g_session.last_progress_bucket) {
                    g_session.last_progress_bucket = bucket;
                    if (g_session.config->diagnostics) {
                        char text[224];
                        std::snprintf(text, sizeof(text), "Sustained dash animation progress: time=%.3f, next=%.3f, transition=%d, length=%.3f, clip_loop=%s, deferred=%u",
                            frame.time, frame.next_time, frame.transitioning, frame.length, ClipLoopText(frame.clip_loop), g_session.natural_end_deferrals);
                        Log(text);
                    }
                }
            }
        }
    }
    // Run the game's tick after any targeted same-side blend request.
    g_pre_late(component, delta, method);
}
bool __fastcall CheckDetour(void* component, const void* method) {
    // 1.6.3 inlines _TickStatePerformInterrupt into PreLateTick, but its
    // condition-3 delegate still invokes ShouldInterruptSpDash through this
    // entry (CharacterAnimationComponent.cctor registers that exact target).
    // Always retain the original result for every other role and perform.
    bool result = g_check(component, method);
    return result && !ShouldHold(component);
}
void __fastcall InterruptDetour(void* component, const void* method) {
    if (component == g_session.component && HoldContext(component)) {
        ++g_session.spdash_interrupts_suppressed;
        if (g_session.config->diagnostics && g_session.spdash_interrupts_suppressed == 1)
            Log("Sustained dash: suppressed InterruptSpDashPerform while the selected special state is active (v7).");
        return;
    }
    if (component == g_session.component) Cancel("native dash interruption", false);
    g_interrupt(component, method);
}
void __fastcall ForceDetour(void* component, const void* method) {
    if (component == g_session.component) Cancel("force stop", false);
    g_force(component, method);
}
void __fastcall ForceCurrentDetour(void* component, bool stop_animation, const void* method) {
    if (component == g_session.component) Cancel("force current perform", false);
    g_force_current(component, stop_animation, method);
}
void __fastcall ReleaseDetour(void* component, bool full, const void* method) {
    ReleaseAccessoryComponent(component);
    if (component == g_session.component) Cancel("component release", false, false);
    if (component == g_external_owner.component) RestoreExternal(true);
    if (component == g_pose_owner.component) ReleasePoseOverlay(true);
    g_release(component, full, method);
}

std::string_view Trim(std::string_view s) {
    constexpr char whitespace[] = " \t\r\n";
    auto first = s.find_first_not_of(whitespace);
    if (first == s.npos) return {};
    return s.substr(first, s.find_last_not_of(whitespace) - first + 1);
}
bool ParseBool(std::string_view s, bool& out) {
    if (s == "true" || s == "1") { out = true; return true; }
    if (s == "false" || s == "0") { out = false; return true; }
    return false;
}
// Comma separated codenames, matching CharacterProfile::codename. An empty list
// is legitimate: it is what the UI writes when every character switch is off.
// Names this build does not know are ignored so a newer UI stays compatible.
bool ParseCharacters(std::string_view s, uint32_t& out) {
    out = 0;
    while (!s.empty()) {
        auto comma = s.find(',');
        auto name = Trim(s.substr(0, comma));
        s = comma == s.npos ? std::string_view{} : s.substr(comma + 1);
        if (name.empty()) continue;
        for (size_t i = 0; i < kCharacterCount; ++i)
            if (name == kCharacters[i].codename) { out |= 1u << i; break; }
    }
    return true;
}
bool ParseFadeSeconds(std::string_view value, float& result) {
    const std::string text(value);
    char* end = nullptr;
    result = std::strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(result) && result >= 0 && result <= 1;
}
BE_Result BE_CALL Configure(const char* raw) {
    auto config = std::make_shared<Configuration>();
    std::string_view remaining = raw ? raw : "";
    bool valid = true;
    bool schema_present = false;
    while (!remaining.empty()) {
        auto end = remaining.find('\n');
        auto line = Trim(remaining.substr(0, end));
        remaining = end == remaining.npos ? std::string_view{} : remaining.substr(end + 1);
        if (line.empty() || line.front() == '[' || line.front() == ';' || line.front() == '#') continue;
        auto equals = line.find('=');
        if (equals == line.npos) { valid = false; break; }
        auto key = Trim(line.substr(0, equals));
        auto value = Trim(line.substr(equals + 1));
        if (key == "schema_version") { schema_present = true; valid &= value == "1" || value == "2" || value == "3"; }
        else if (key == "enabled") valid &= ParseBool(value, config->enabled);
        else if (key == "diagnostics") valid &= ParseBool(value, config->diagnostics);
        else if (key == "external_loop") valid &= ParseBool(value, config->external_loop);
        else if (key == "liino_clean") valid &= ParseBool(value, config->liino_clean);
        else if (key == "characters") valid &= ParseCharacters(value, config->characters);
        else if (key == "jsspsi_capture") valid &= ParseBool(value, config->jsspsi_capture);
        else if (key == "jsspsi_seam_fix") valid &= ParseBool(value, config->jsspsi_seam_fix);
        else if (key == "jsspsi_accessory_loop") valid &= ParseBool(value, config->jsspsi_accessory_loop);
        else if (key == "jsspsi_accessory_mask") valid &= AccessoryMask(value, config->jsspsi_accessory_mask);
        else if (key == "jsspsi_accessory_fade") valid &= ParseFadeSeconds(value, config->jsspsi_accessory_fade);
    }
    if (!valid || (config->enabled && !schema_present)) {
        // A bad live edit must never leave the old hold enabled indefinitely.
        config->enabled = false;
        Log("Invalid action configuration; special dash disabled.");
    }
    if (*g_config.load() == *config) return BE_Result_Ok;
    g_config.store(config);
    if (config->enabled && config->diagnostics) {
        char text[160];
        std::snprintf(text, sizeof(text), "Sustained dash constants: SpDashEnd=%d, ForceExit=%d, DashMode=%d, GroundedMode=%d, SprintGait=%d",
            g_sp_dash_end, g_force_exit, g_dash_mode, g_grounded_mode, g_sprint);
        Log(text);
    }
    Log(!config->enabled ? "Actions disabled." : config->external_loop ?
        "Actions enabled: v12 bone-pose overlay; native movement/Animator retained, no external clip load." :
        "Actions enabled: v9 loop; external pose overlay disabled.");
    if (config->enabled) {
        char text[192];
        int written = std::snprintf(text, sizeof(text), "Sustained dash characters:");
        for (size_t i = 0; i < kCharacterCount && written > 0 && written < (int)sizeof(text); ++i)
            if (config->characters & (1u << i))
                written += std::snprintf(text + written, sizeof(text) - written, " %s", kCharacters[i].codename);
        if (!config->characters) std::snprintf(text + written, sizeof(text) - written, " none");
        Log(text);
    }
    return BE_Result_Ok;
}

using MethodFlagsFn = uint32_t(*)(const void*, uint32_t*);
void InitializeJsspsiContracts(MethodFlagsFn flags) {
    for (int id=JsspsiTmpAnimator;id<MethodCount;++id) {
        auto& method=g_methods[id].resolved;
        uint32_t impl=0;
        const bool expected_static=id==JsspsiPostEvent || id==JsspsiStopPlaying;
        if (!method.method_info || !method.method_pointer ||
            ((flags(method.method_info,&impl)&0x10u)!=0)!=expected_static) {
            Log((std::string("Sustained dash Jsspsi contracts: unavailable ") + g_methods[id].desc.class_name + "." + g_methods[id].desc.method_name).c_str());
            method={};
        }
    }
    // Ending-splash suppression requires instance methods for perform/main-character identity.
    g_ending_splash_skip_contract=g_effect_action_play != nullptr;
    for (int id:{VisualActionHandle,VisualActionData,ActionIndex})
        if (!g_fields[id].resolved.field_info) g_ending_splash_skip_contract=false;
    for (int id:{EffectActionPlay,VisualPerformId,VisualPerformEntity,EntityValid,GetCharCtrl,IsMain}) {
        uint32_t impl=0;
        if (!g_methods[id].resolved.method_info || (flags(g_methods[id].resolved.method_info,&impl)&0x10u)) g_ending_splash_skip_contract=false;
    }
    struct Hook { MethodId id; void* detour; void** original; };
    const Hook hooks[]{
        {JsspsiPlayRenderVfx,reinterpret_cast<void*>(&PlayRenderVfxDetour),reinterpret_cast<void**>(&g_play_render_vfx)},
        {JsspsiPlayVfx,reinterpret_cast<void*>(&PlayVfxDetour),reinterpret_cast<void**>(&g_play_vfx)},
        {JsspsiVfxTick,reinterpret_cast<void*>(&VfxTickDetour),reinterpret_cast<void**>(&g_vfx_tick)},
        {JsspsiVfxDelta,reinterpret_cast<void*>(&VfxDeltaDetour),reinterpret_cast<void**>(&g_vfx_delta)},
        {JsspsiClothUpdate,reinterpret_cast<void*>(&ClothUpdateDetour),reinterpret_cast<void**>(&g_cloth_update)},
        {JsspsiPostEvent,reinterpret_cast<void*>(&PostEventDetour),reinterpret_cast<void**>(&g_post_event)},
    };
    int installed=0;
    for (const auto& hook:hooks) {
        auto& method=g_methods[hook.id].resolved;
        *hook.original=nullptr;
        if (hook.id==JsspsiVfxDelta && (!g_fields[JsspsiVfxOwner].resolved.field_info || g_fields[JsspsiVfxOwner].resolved.offset<=0)) continue;
        if (method.method_info && g_host->create_hook(g_host->context,kId,method.method_pointer,hook.detour,hook.original)==BE_Result_Ok) ++installed;
        else *hook.original=nullptr;
    }
    BE_ResolvedClassV1 transform{};
    if (g_host->resolve_class(g_host->context,kUnityCore,"UnityEngine","Transform",&transform)==BE_Result_Ok) g_transform_type=transform.type_object;
    g_hierarchy_contract=g_pose_contract && g_transform_type && g_methods[ComponentGameObject].resolved.method_info && g_methods[ClipName].resolved.method_info;
    char text[256];
    std::snprintf(text,sizeof(text),"Sustained dash Jsspsi contracts: hooks=%d/6 tmp=%d hierarchy=%d vfx=%d clock=%d sound=%d seam=%d accessory=%d splashes=%d",
        installed,g_fields[JsspsiTmpObjects].resolved.field_info && g_methods[JsspsiTmpAnimator].resolved.method_info,
        g_transform_type && g_methods[ComponentGameObject].resolved.method_info,
        g_play_render_vfx && g_play_vfx && g_vfx_tick,g_vfx_delta!=nullptr,
        g_post_event && g_methods[JsspsiStopPlaying].resolved.method_info,g_hierarchy_contract,
        g_hierarchy_contract && g_pose_leases,g_ending_splash_skip_contract);
    Log(text);
}

BE_Result BE_CALL Initialize(const BE_HostApiV1* host) {
    // Android links the registry into this library for Jsspsi seam/accessory writers.
    // Its v12 overlay still uses the upstream lease-free path; Windows v12 and
    // Jsspsi both use the shared Host registry obtained through the exported API.
#if defined(_WIN32) || defined(__ANDROID__) || defined(BE_TEST_PC_POSE)
#if defined(__ANDROID__)
    g_pose_leases = BetterEndfieldNext_GetPoseLeaseApiV1();
#else
    auto lease_getter = reinterpret_cast<BE_GetPoseLeaseApiV1Fn>(GetProcAddress(
        GetModuleHandleW(L"BetterEndfieldNext.Host.dll"), "BetterEndfieldNext_GetPoseLeaseApiV1"));
    g_pose_leases = lease_getter ? lease_getter() : nullptr;
#endif
    if (g_pose_leases && (g_pose_leases->version != 1 || !g_pose_leases->acquire ||
        !g_pose_leases->owns || !g_pose_leases->release)) g_pose_leases = nullptr;
    g_pose_lease_ready.store(g_pose_leases != nullptr, std::memory_order_release);
#endif
    if (!host || host->abi_version != BETTER_ENDFIELD_NEXT_MODULE_ABI_V1 ||
        !host->resolve_method || !host->resolve_field || !host->resolve_class || !host->runtime_invoke ||
        !host->object_unbox || !host->field_get_value_object ||
        !host->copy_managed_string || !host->gchandle_new || !host->gchandle_free ||
        !host->create_hook || !host->release_module_hooks) return BE_Result_InvalidArgument;
    g_host = host;
    for (auto& method : g_methods) {
        if (host->resolve_method(host->context, &method.desc, &method.resolved) != BE_Result_Ok) {
            if (&method == &g_methods[StateLoop]) {
                method.resolved = {};
                Log("Optional action diagnostic unavailable: AnimatorStateInfo.get_loop; clip_loop=unknown.");
                continue;
            }
            Log((std::string("Missing action method: ") + method.desc.class_name + "." + method.desc.method_name).c_str());
            if (&method >= &g_methods[JsspsiTmpAnimator]) { method.resolved = {}; continue; }
            if (&method == &g_methods[MeshGroupShow]) { g_mesh_group_contract = false; continue; }
            if (&method == &g_methods[EffectActionPlay]) { g_liino_teardown_contract = false; continue; }
            if (&method == &g_methods[EffectManualFollow]) { g_effect_follow_contract = false; continue; }
            if (&method >= &g_methods[VisualPerformId]) { g_liino_clean_contract = false; continue; }
            if (&method >= &g_methods[PoseTail]) { g_pose_contract = false; continue; }
            if (&method >= &g_methods[BundleLoad]) { g_external_contract = false; continue; }
            return BE_Result_ContractMismatch;
        }
    }
    for (auto& field : g_fields) {
        if (host->resolve_field(host->context, &field.desc, &field.resolved) != BE_Result_Ok) {
            Log((std::string("Missing action field: ") + field.desc.field_name).c_str());
            if (&field >= &g_fields[JsspsiTmpObjects]) { field.resolved = {}; continue; }
            if (&field >= &g_fields[VisualActionHandle]) { g_liino_teardown_contract = false; continue; }
            return BE_Result_ContractMismatch;
        }
    }
    // Protect against the easy-to-miss static ShouldInterruptSpDash ABI.
    auto flags = reinterpret_cast<MethodFlagsFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_method_get_flags"));
    if (!flags) return BE_Result_ContractMismatch;
    for (int i = Start; i <= Release; ++i) {
        uint32_t impl = 0;
        bool is_static = (flags(g_methods[i].resolved.method_info, &impl) & 0x10u) != 0;
        if (is_static != (i == ShouldInterrupt)) {
            Log("Action hook static/instance contract mismatch.");
            return BE_Result_ContractMismatch;
        }
    }
    bool ok = true;
    // Host resolve_field deliberately requires an instance offset. Enum literals
    // may have no such offset: get this constant through metadata, never a raw
    // offset or a hard-coded numeric Sprint value.
    using FindFieldFn = void*(*)(const void*, const char*);
    auto find_field = reinterpret_cast<FindFieldFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_class_get_field_from_name"));
    if (!find_field) return BE_Result_ContractMismatch;
    auto static_field = [&](const char* ns, const char* cls, const char* name) -> const void* {
        BE_ResolvedClassV1 resolved{};
        if (host->resolve_class(host->context, kGame, ns, cls, &resolved) != BE_Result_Ok) return nullptr;
        return find_field(resolved.class_info, name);
    };
    auto constant = [&](const char* ns, const char* cls, const char* name) {
        const void* field = static_field(ns, cls, name);
        if (!field) { ok = false; return -1; }
        return Unbox<int>(host->field_get_value_object(host->context, field, nullptr), ok);
    };
    g_sprint = constant(kCore, "MovementComponent.GroundedMoveGait", "Sprint");
    g_dash_mode = constant(kCore, "MovementComponent.MoveMode", "Dash");
    g_grounded_mode = constant(kCore, "MovementComponent.MoveMode", "Grounded");
    g_perform_end = constant("Beyond.Gameplay", "CharInteractPerformEnums.CharInteractPerformState", "End");
    g_sp_dash_end = constant("Beyond.Gameplay", "CharInteractPerformEnums.InterruptReason", "SpDashEnd");
    g_force_exit = constant("Beyond.Gameplay", "CharInteractPerformEnums.InterruptType", "ForceExit");
    // Resolve model-part values independently; a missing optional visual
    // contract must not disable the other character's dash.
    for (int i = 0; i < 2; ++i) {
        const void* field = static_field("Beyond.Gameplay", "ModelPartEnum", i == 0 ? "MeshGroup1" : "MeshGroup2");
        bool group_ok = field != nullptr;
        if (field) g_liino_mesh_groups[i] = Unbox<int>(host->field_get_value_object(host->context, field, nullptr), group_ok);
        if (!group_ok || g_liino_mesh_groups[i] <= 0) g_mesh_group_contract = false;
    }
    auto other_constant = [&](const char* assembly, const char* ns, const char* cls, const char* name) {
        BE_ResolvedClassV1 resolved{};
        if (host->resolve_class(host->context, assembly, ns, cls, &resolved) != BE_Result_Ok) { ok = false; return -1; }
        auto field = find_field(resolved.class_info, name);
        if (!field) { ok = false; return -1; }
        return Unbox<int>(host->field_get_value_object(host->context, field, nullptr), ok);
    };
    g_blend_style = other_constant(kUnity, "UnityEngine", "AnimationBlendStyle", "HermiteCubic");
    g_blend_interrupt = other_constant(kUnity, "UnityEngine", "AnimatorTransitionInterruptionSource", "CurrentThenNext");
    g_audio_none = other_constant("Audio.Beyond.dll", "Beyond.Audio", "AudioCallbackType", "None");
    g_particle_stop_clear = other_constant(kParticles, "UnityEngine", "ParticleSystemStopBehavior", "StopEmittingAndClear");
    using NewStringFn = void*(*)(const char*);
    auto new_string = reinterpret_cast<NewStringFn>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_string_new"));
    if (!new_string) return BE_Result_ContractMismatch;
    for (size_t i = 0; i < kCharacterCount; ++i) {
        const char* event = kCharacters[i].flying_stop_event;
        if (!event) continue; // This character has no continuous audio to defer.
        void* stop_name = new_string(event);
        if (!stop_name) return BE_Result_Failed;
        void* hash_args[]{stop_name};
        g_flying_stop_hashes[i] = Unbox<uint32_t>(Invoke(AudioHash, nullptr, hash_args, ok), ok);
        if (!ok || !g_flying_stop_hashes[i]) return BE_Result_ContractMismatch;
    }
    g_hash_fields[0] = static_field(kView, kCharacterComp, "HASH_SP_DASH_L");
    g_hash_fields[1] = static_field(kView, kCharacterComp, "HASH_SP_DASH_R");
    g_locomotion_fields[0] = static_field(kView, "CharacterAnimationBlackboard", "HASH_STATE_RUN");
    g_locomotion_fields[1] = static_field(kView, "CharacterAnimationBlackboard", "HASH_STATE_SPRINT");
    uint32_t impl = 0;
    if (!ok || !g_hash_fields[0] || !g_hash_fields[1] || !g_locomotion_fields[0] || !g_locomotion_fields[1] ||
        (flags(g_methods[SetDashing].resolved.method_info, &impl) & 0x10u)) return BE_Result_ContractMismatch;
    for (MethodId id : {FlowTick, PerformClear, AddCommand, CrossFade, TryInterruptCommand,
        DoExit, ShowObject, AudioMonoPost, AudioMonoComponent, EffectDuration, EffectFinish, EffectStop})
        if (flags(g_methods[id].resolved.method_info, &impl) & 0x10u) return BE_Result_ContractMismatch;
    for (MethodId id : {AudioEntityPost, AudioHash, UnityAlive})
        if (!(flags(g_methods[id].resolved.method_info, &impl) & 0x10u)) return BE_Result_ContractMismatch;
    auto game_module = GetModuleHandleW(L"GameAssembly.dll");
    g_object_class = reinterpret_cast<ObjectClassFn>(GetProcAddress(game_module, "il2cpp_object_get_class"));
    g_class_method = reinterpret_cast<ClassMethodFn>(GetProcAddress(game_module, "il2cpp_class_get_method_from_name"));
    g_class_parent = reinterpret_cast<ClassParentFn>(GetProcAddress(game_module, "il2cpp_class_get_parent"));
    using ClassTypeFn = const void*(*)(void*);
    using TypeObjectFn = void*(*)(const void*);
    auto class_type = reinterpret_cast<ClassTypeFn>(GetProcAddress(game_module, "il2cpp_class_get_type"));
    auto type_object = reinterpret_cast<TypeObjectFn>(GetProcAddress(game_module, "il2cpp_type_get_object"));
    BE_ResolvedClassV1 particle_class{};
    if (!g_object_class || !g_class_method || !g_class_parent || !class_type || !type_object ||
        host->resolve_class(host->context, kParticles, "UnityEngine", "ParticleSystem", &particle_class) != BE_Result_Ok)
        return BE_Result_ContractMismatch;
    g_particle_type = type_object(class_type(const_cast<void*>(particle_class.class_info)));
    if (!g_particle_type) return BE_Result_ContractMismatch;
    g_particle_type_root = host->gchandle_new(host->context, g_particle_type, 1);
    if (!g_particle_type_root) return BE_Result_Failed;
    struct Hook { MethodId id; void* detour; void** original; };
    Hook hooks[]{
        {TryInterruptCommand, reinterpret_cast<void*>(&TryInterruptCommandDetour), reinterpret_cast<void**>(&g_try_interrupt_command)},
        {FlowTick, reinterpret_cast<void*>(&FlowDetour), reinterpret_cast<void**>(&g_flow_tick)},
        {DoExit, reinterpret_cast<void*>(&DoExitDetour), reinterpret_cast<void**>(&g_do_exit)},
        {ShowObject, reinterpret_cast<void*>(&ShowObjectDetour), reinterpret_cast<void**>(&g_show)},
        {AudioMonoPost, reinterpret_cast<void*>(&AudioMonoDetour), reinterpret_cast<void**>(&g_audio_mono)},
        {AudioEntityPost, reinterpret_cast<void*>(&AudioEntityDetour), reinterpret_cast<void**>(&g_audio_entity)},
        {EffectDuration, reinterpret_cast<void*>(&EffectDurationDetour), reinterpret_cast<void**>(&g_effect_duration)},
        {EffectFinish, reinterpret_cast<void*>(&EffectFinishDetour), reinterpret_cast<void**>(&g_effect_finish)},
        {EffectStop, reinterpret_cast<void*>(&EffectStopDetour), reinterpret_cast<void**>(&g_effect_stop)},
        {PerformClear, reinterpret_cast<void*>(&PerformClearDetour), reinterpret_cast<void**>(&g_perform_clear)},
        {AddCommand, reinterpret_cast<void*>(&CommandDetour), reinterpret_cast<void**>(&g_add_command)},
        {SetDashing, reinterpret_cast<void*>(&DashingDetour), reinterpret_cast<void**>(&g_set_dashing)},
        {Start, reinterpret_cast<void*>(&StartDetour), reinterpret_cast<void**>(&g_start)},
        {PreLate, reinterpret_cast<void*>(&PreLateDetour), reinterpret_cast<void**>(&g_pre_late)},
        {ShouldInterrupt, reinterpret_cast<void*>(&CheckDetour), reinterpret_cast<void**>(&g_check)},
        {Interrupt, reinterpret_cast<void*>(&InterruptDetour), reinterpret_cast<void**>(&g_interrupt)},
        {ForceStop, reinterpret_cast<void*>(&ForceDetour), reinterpret_cast<void**>(&g_force)},
        {ForceCurrent, reinterpret_cast<void*>(&ForceCurrentDetour), reinterpret_cast<void**>(&g_force_current)},
        {Release, reinterpret_cast<void*>(&ReleaseDetour), reinterpret_cast<void**>(&g_release)},
    };
    for (const auto& hook : hooks) {
        if (host->create_hook(host->context, kId, g_methods[hook.id].resolved.method_pointer,
                hook.detour, hook.original) != BE_Result_Ok) {
            host->release_module_hooks(host->context, kId);
            host->gchandle_free(host->context, g_particle_type_root);
            g_particle_type_root = 0;
            g_particle_type = nullptr;
            Log("Action hooks rolled back after installation failure.");
            return BE_Result_Failed;
        }
    }
    if (g_liino_teardown_contract &&
        ((flags(g_methods[EffectActionPlay].resolved.method_info, &impl) & 0x10u) ||
         host->create_hook(host->context, kId, g_methods[EffectActionPlay].resolved.method_pointer,
             reinterpret_cast<void*>(&EffectActionPlayDetour), reinterpret_cast<void**>(&g_effect_action_play)) != BE_Result_Ok))
        g_liino_teardown_contract = false;
    if (!g_liino_teardown_contract) Log("Sustained dash Liino: teardown hook unavailable; material dissolve follows native timing.");
    if (g_effect_follow_contract && (flags(g_methods[EffectManualFollow].resolved.method_info, &impl) & 0x10u))
        g_effect_follow_contract = false;
    if (!g_effect_follow_contract) Log("Sustained dash Liino: final-pose effect follow refresh unavailable.");
    if (g_mesh_group_contract && host->create_hook(host->context, kId, g_methods[MeshGroupShow].resolved.method_pointer,
            reinterpret_cast<void*>(&MeshGroupShowDetour), reinterpret_cast<void**>(&g_mesh_group_show)) != BE_Result_Ok) {
        g_mesh_group_contract = false;
        Log("Sustained dash: mesh group hook unavailable; model parts follow their native timing.");
    }
    if (g_liino_clean_contract) {
        for (int id : {VisualPerformId, VisualPerformEntity})
            if (flags(g_methods[id].resolved.method_info, &impl) & 0x10u) g_liino_clean_contract = false;
    }
    g_liino_clean_contract = g_liino_clean_contract && g_mesh_group_contract && g_liino_teardown_contract;
    Log(g_liino_clean_contract ? "Liino clean dash available: native perform entry/exit VFX and MeshGroup1/2 omitted when liino_clean=true." :
        "Liino clean dash unavailable: required visual hooks missing; retained appearance fallback.");
    if (g_pose_contract && host->create_hook(host->context, kId, g_methods[PoseTail].resolved.method_pointer,
            reinterpret_cast<void*>(&PoseTailDetour), reinterpret_cast<void**>(&g_pose_tail)) != BE_Result_Ok) {
        g_pose_contract = false;
        Log("Sustained dash v12: TailLate hook unavailable; pose overlay disabled, native v9 hold remains.");
    }
    InitializeJsspsiContracts(flags);
    Log("Actions module ready; scoped to character special dashes, disabled by default.");
    return BE_Result_Ok;
}
void BE_CALL Shutdown() {
    g_stopping = true;
    if (!g_host) return;
    g_host->release_module_hooks(g_host->context, kId);
    std::lock_guard<std::recursive_mutex> feature_lock(g_feature_mutex);
    ReleaseSeam();
    if (g_dash_pose) g_dash_pose->End();
    // Host unload is during shutdown. Do not invoke Unity on this worker thread.
    EndAccessory();
    Cancel("module shutdown", false, false);
    g_dash_pose.reset();
    RestoreExternal(false);
    FreeExternalAssets(false);
    ReleasePoseOverlay(false);
    if (g_particle_type_root) g_host->gchandle_free(g_host->context, g_particle_type_root);
    g_particle_type_root = 0;
    g_particle_type = nullptr;
}
const BE_ModuleApiV1 kApi{{kId, "Sustained Dash", "1.14.0", BETTER_ENDFIELD_NEXT_MODULE_ABI_V1},
    &Initialize, &Configure, &Shutdown};
} // namespace
} // namespace BetterEndfieldNext::Actions

BE_EXPORT const BE_ModuleApiV1* BE_CALL BetterEndfieldNext_GetModuleApiV1() {
    return &BetterEndfieldNext::Actions::kApi;
}

// Camera checks this handshake before permitting a body writer alongside Actions.
BE_EXPORT uint32_t BE_CALL BetterEndfieldNext_ActionsPoseLeaseVersionV1() {
    return BetterEndfieldNext::Actions::g_pose_lease_ready.load(std::memory_order_acquire) ? 1u : 0u;
}
