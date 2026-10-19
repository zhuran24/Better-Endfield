// Exercise the production configuration and detours using the existing managed
// host fixture. This does not establish Unity/IL2CPP ABI or rendered output.
#include "effects_test_host.h"
constexpr uintptr_t kStopPlayingMethod = 0x10000;
int delta_calls = 0, sound_stops = 0;
float tick_delta = -1;
void __fastcall TickVfxFake(void *, float delta, const void *) {
    tick_delta = delta;
}
float __fastcall DeltaFake(void *, bool, const void *) {
    ++delta_calls;
    return .016f;
}
uint32_t __fastcall PostFake(uint32_t, uint64_t, int, void *, void *, const void *) {
    return 77;
}
bool throw_effect = false;
int render_calls = 0, hook_calls = 0;
int bad_static = -1;
void __fastcall RenderFake(void *, const void *) {
    ++render_calls;
}
uint32_t FlagsFake(const void *method, uint32_t *) {
    int id = int(uintptr_t(method) - 1);
    bool value = id == JsspsiPostEvent || id == JsspsiStopPlaying;
    if (id == bad_static)
        value = !value;
    return value ? 0x10u : 0;
}
BE_Result BE_CALL HookFake(void *, const char *, void *, void *, void **original) {
    ++hook_calls;
    *original = reinterpret_cast<void *>(uintptr_t(1));
    return BE_Result_Ok;
}
BE_Result BE_CALL ClassFake(void *, const char *, const char *, const char *, BE_ResolvedClassV1 *out) {
    out->type_object = &visual_identity;
    return BE_Result_Ok;
}
void *BE_CALL InvokeJsspsiFake(void *context, const void *method, void *instance, void **args, void **exception) {
    if (throw_effect && uintptr_t(method) == EffectName + 1)
        throw std::bad_alloc();
    if (method == reinterpret_cast<void *>(kStopPlayingMethod)) {
        *exception = nullptr;
        CHECK(*static_cast<uint32_t *>(args[0]) == 77 && *static_cast<int *>(args[1]) == 300);
        ++sound_stops;
        return nullptr;
    }
    return InvokeFake(context, method, instance, args, exception);
}
unsigned action_data_reads = 0, actor_reads = 0;
void* BE_CALL CountActionFields(void* context, const void* field, void* instance) {
    const auto id = static_cast<FieldId>(uintptr_t(field) - 1);
    if (id == VisualActionData) ++action_data_reads;
    if (id == ActionIndex) ++actor_reads;
    return FieldFake(context, field, instance);
}
int configuration_interrupts = 0;
void __fastcall ConfigurationInterrupt(void*, const void*) { ++configuration_interrupts; }
void __fastcall ConfigurationPreLate(void*, float, const void*) {}
void TestConfigurationRelevance() {
    const std::string base = "schema_version=3\nenabled=true\ncharacters=aglina,liino,jsspsi\n";
    g_pre_late = ConfigurationPreLate;
    g_interrupt = ConfigurationInterrupt;
    // Each boundary is independently the first consumer of the replacement.
    for (int role : {0, 1, 2}) for (int first : {0, 1, 2}) {
        for (const char* edit : {"jsspsi_capture=true", "jsspsi_seam_fix=false", "jsspsi_accessory_loop=false",
                                 "jsspsi_accessory_mask=0x01", "jsspsi_accessory_fade=0.8"}) {
            g_session = {};
            Configure(base.c_str());
            g_session.component = &visual_identity;
            g_session.handle = &visual_handle;
            g_session.profile = &kCharacters[role];
            g_session.config = g_config.load();
            g_session.policy.active = true;
            g_session.loop.c = kCharacters[role].loop;
            for (auto& effect : g_session.effects) effect.disabled = true;
            configuration_interrupts = 0;
            Configure((base + edit + "\n").c_str());
            CHECK(g_session.config != g_config.load());
            if (first == 1) CHECK(ShouldHold(&visual_identity) == (role != 2));
            if (first == 2) CHECK(HoldContext(&visual_identity) == (role != 2));
            PreLateDetour(&visual_identity, .016f, nullptr);
            CHECK(bool(g_session.component) == (role != 2));
            CHECK(configuration_interrupts == (role == 2 ? 1 : 0));
            if (role != 2) CHECK(g_session.config == g_config.load() && g_session.policy.active);
        }
    }
    // Shared settings retain the original invalidation rule for every character.
    for (int role : {0, 1, 2}) for (const char* edit : {"enabled=false", "diagnostics=false",
            "external_loop=true", "liino_clean=true", "characters="}) {
        g_session = {}; Configure(base.c_str());
        g_session.component = &visual_identity; g_session.handle = &visual_handle;
        g_session.profile = &kCharacters[role]; g_session.config = g_config.load();
        Configure((base + edit + "\n").c_str());
        configuration_interrupts = 0;
        PreLateDetour(&visual_identity, .016f, nullptr);
        CHECK(!g_session.component && configuration_interrupts == 1);
    }
    g_session = {};
}
int main() {
    BE_HostApiV1 host{};
    host.runtime_invoke = InvokeJsspsiFake;
    host.object_unbox = UnboxFake;
    host.field_get_value_object = FieldFake;
    host.copy_managed_string = CopyStringFake;
    host.gchandle_free = FreeFake;
    host.gchandle_new = PinFake;
    g_host = &host;
    for (int i = 0; i < MethodCount; ++i)
        g_methods[i].resolved.method_info = reinterpret_cast<void *>(uintptr_t(i + 1));
    for (int i = 0; i < FieldCount; ++i)
        g_fields[i].resolved.field_info = reinterpret_cast<void *>(uintptr_t(i + 1));
    Configure("schema_version=3\nenabled=true\ncharacters=jsspsi\n");
    auto c = g_config.load();
    CHECK(c->enabled && c->characters == 4 && c->jsspsi_seam_fix && c->jsspsi_accessory_loop);
    CHECK(!c->jsspsi_capture && c->jsspsi_accessory_mask == 3 && c->jsspsi_accessory_fade == .4f);
    Configuration off_a, off_b;
    off_a.jsspsi_accessory_loop = off_b.jsspsi_accessory_loop = false;
    off_b.jsspsi_accessory_mask = 0x4f;
    off_b.jsspsi_accessory_fade = .8f;
    CHECK(!(off_a == off_b) && !(off_b == off_a));
    off_a.jsspsi_accessory_loop = off_b.jsspsi_accessory_loop = true;
    CHECK(!(off_a == off_b));
    Configure("schema_version=4\nenabled=true\n");
    CHECK(!g_config.load()->enabled);
    Configure("enabled=true\n");
    CHECK(!g_config.load()->enabled);
    Configure(
        "schema_version=3\nenabled=true\njsspsi_capture=true\njsspsi_accessory_mask=0x4F\njsspsi_accessory_fade=0.8\n");
    CHECK(g_config.load()->jsspsi_capture && g_config.load()->jsspsi_accessory_mask == 0x4f &&
          g_config.load()->jsspsi_accessory_fade == .8f);
    for (const char *bad : {"nan", "inf", "-0.1", "1.1", "0.4junk", ""}) {
        Configure((std::string("schema_version=3\nenabled=true\njsspsi_accessory_fade=") + bad + "\n").c_str());
        CHECK(!g_config.load()->enabled);
    }
    // Unknown keys cannot enable features or override the supported fixed policy.
    Configure("schema_version=3\nenabled=true\ncharacters=jsspsi\ncapture=true\njsspsi_loop=bad\njsspsi_side_loop="
              "bad\njsspsi_hold_effects=bad\njsspsi_skip_effects=bad\njsspsi_skip_logic=bad\njsspsi_freeze_track="
              "bad\njsspsi_freeze_vfx=bad\njsspsi_stop_dash_sound=bad\njsspsi_hold_from_start=bad\njsspsi_cloth_"
              "physics=bad\njsspsi_seam_window=bad\njsspsi_seam_debug_tilt=bad\n");
    CHECK(g_config.load()->enabled && !g_config.load()->jsspsi_capture);
    g_game_thread = GetCurrentThreadId();
    g_effect_action_play = ActionFake;
    g_ending_splash_skip_contract = true;
    // No Session is armed: these actions also execute inside native StartSpDash.
    CHECK(!g_session.component);
    for (const char *name :
         {"CharIntPerform_Jsspsi_Spdash", "CharIntPerform_Jsspsi_SpdashL", "CharIntPerform_Jsspsi_SpdashR"}) {
        perform_id = name;
        CHECK(PerformMatches(const_cast<char *>(name), kCharacters[2].perform_id));
        for (int slot = 0; slot < 22; ++slot) {
            visual_actor = slot;
            const int before = action_calls, calls = unity_calls;
            EffectActionPlayDetour(&visual_identity, nullptr);
            const bool masked = (kCharacters[2].skipped_effect_actors & (1u << slot)) != 0;
            CHECK(action_calls - before == (masked ? 0 : 1));
            if (!masked)
                CHECK(unity_calls == calls);
        }
    }
    CHECK(!PerformMatches(const_cast<char *>("CharIntPerform_Jsspsi_SpdashLR"), kCharacters[2].perform_id));
    for (const char *name : {"CharIntPerform_Aglina_Spdash", "CharIntPerform_Liino_Spdash", "OtherPerform",
                             "CharIntPerform_Jsspsi_SpdashLR"}) {
        perform_id = name;
        for (int actor = 0; actor < 22; ++actor) {
            visual_actor = actor;
            const int before = action_calls, calls = unity_calls;
            EffectActionPlayDetour(&visual_identity, nullptr);
            CHECK(action_calls == before + 1);
            if (!(kJsspsiSkipEffects & (1u << actor)))
                CHECK(unity_calls == calls);
        }
    }
    for (const char *mode :
         {"capture", "non-main", "disabled", "unselected", "contract", "foreign-handle", "identity-error"}) {
        auto config = std::make_shared<Configuration>();
        config->enabled = true;
        config->characters = 4;
        config->jsspsi_capture = !std::strcmp(mode, "capture");
        if (!std::strcmp(mode, "disabled"))
            config->enabled = false;
        if (!std::strcmp(mode, "unselected"))
            config->characters = 3;
        main_character = std::strcmp(mode, "non-main") != 0;
        g_ending_splash_skip_contract = std::strcmp(mode, "contract") != 0;
        action_handle = !std::strcmp(mode, "foreign-handle") ? &visual_other : &visual_handle;
        visual_metadata_error = !std::strcmp(mode, "identity-error");
        g_config.store(config);
        perform_id = "CharIntPerform_Jsspsi_SpdashR";
        for (int actor : {10, 11, 12}) {
            visual_actor = actor;
            const int before = action_calls;
            EffectActionPlayDetour(&visual_identity, nullptr);
            CHECK(action_calls == before + 1);
        }
    }
    host.field_get_value_object = CountActionFields;
    for (uint32_t characters : {0u, 1u, 2u, 3u}) {
        auto selected = std::make_shared<Configuration>();
        selected->enabled = true; selected->characters = characters;
        g_config.store(selected); g_ending_splash_skip_contract = true;
        action_data_reads = actor_reads = 0;
        const auto calls = action_calls;
        EffectActionPlayDetour(&visual_identity, nullptr);
        CHECK(action_data_reads == 0 && actor_reads == 0 && action_calls == calls + 1);
    }
    host.field_get_value_object = FieldFake;
    main_character = true;
    g_ending_splash_skip_contract = true;
    action_handle = &visual_handle;
    visual_metadata_error = false;
    int before = 0;
    CHECK(NameHas("JSSPSI_ink", "jsspsi"));
    auto config = std::make_shared<Configuration>();
    config->enabled = true;
    g_config.store(config);
    g_session = {};
    g_session.component = g_session.entity = &visual_identity;
    g_session.handle = &visual_handle;
    g_session.profile = &kCharacters[2];
    g_session.config = config;
    g_session.policy.active = true;
    g_hashes[0] = 123;
    g_hashes[1] = 456;
    Particle p;
    g_session.effects[0] = Held(p, &visual_identity);
    g_effect_duration = DurationFake;
    EffectDurationDetour(&visual_identity, .016f, nullptr);
    CHECK(duration_delta == .016f);
    g_session.rewound = true;
    EffectDurationDetour(&visual_identity, .016f, nullptr);
    CHECK(duration_delta == 0);
    airborne = true;
    EffectDurationDetour(&visual_identity, .016f, nullptr);
    CHECK(duration_delta == .016f);
    airborne = false;
    struct Controller {
        void *header;
        void *owner;
    } controller{nullptr, &visual_identity};
    g_vfx_delta = DeltaFake;
    g_fields[JsspsiVfxOwner].resolved.offset = sizeof(void *);
    g_session.vfx_helpers[0] = &visual_identity;
    g_session.vfx_helper_count = 1;
    CHECK(VfxDeltaDetour(&controller, false, nullptr) == .016f);
    g_session.vfx_frozen = true;
    CHECK(VfxDeltaDetour(&controller, false, nullptr) == 0 && delta_calls == 2);
    g_vfx_tick = TickVfxFake;
    g_session.vfx[0] = &visual_identity;
    g_session.vfx_count = 1;
    airborne = true;
    VfxTickDetour(&visual_identity, .016f, nullptr);
    CHECK(tick_delta == .016f);
    airborne = false;
    VfxTickDetour(&visual_identity, .016f, nullptr);
    CHECK(tick_delta == 0);
    g_session.policy.active = false;
    VfxTickDetour(&visual_identity, .016f, nullptr);
    CHECK(tick_delta == .016f);
    g_session.policy.active = true;
    controller.owner = &visual_other;
    CHECK(VfxDeltaDetour(&controller, false, nullptr) == .016f);
    controller.owner = &visual_identity;
    g_session.policy.active = false;
    CHECK(VfxDeltaDetour(&controller, false, nullptr) == .016f);
    g_session.policy.active = true;
    g_mesh_group_show = MeshFake;
    g_liino_mesh_groups[0] = 1;
    g_liino_mesh_groups[1] = 2;
    MeshGroupShowDetour(&visual_handle, &visual_identity, 1, false, nullptr);
    CHECK(mesh_calls == 0);
    MeshGroupShowDetour(&visual_handle, &visual_identity, 1, true, nullptr);
    CHECK(mesh_calls == 1);
    g_post_event = PostFake;
    g_session.rewound = false;
    {
        AudioOriginScope origin(false);
        PostEventDetour(12, 1, 0, nullptr, nullptr, nullptr);
    }
    CHECK(!g_session.dash_sound_playing);
    {
        AudioOriginScope origin(true);
        PostEventDetour(12, 1, 0, nullptr, nullptr, nullptr);
    }
    CHECK(g_session.dash_sound_playing == 77 && g_session.dash_sound_event == 12);
    g_session.rewound = true;
    g_session.dash_sound_playing = 0;
    g_audio_origin = true;
    PostEventDetour(1, 1, 0, nullptr, nullptr, nullptr);
    g_audio_origin = false;
    CHECK(g_session.dash_sound_playing == 0);
    g_session.dash_sound_playing = 77;
    g_session.rewound = true;
    g_methods[JsspsiStopPlaying].resolved.method_info = reinterpret_cast<void *>(kStopPlayingMethod);
    StopDashSound();
    StopDashSound();
    CHECK(sound_stops == 1 && !g_session.dash_sound_playing);
    // No optional name getter on disabled/other-character sessions.
    g_play_render_vfx = RenderFake;
    g_session.profile = &kCharacters[0];
    before = unity_calls;
    PlayRenderVfxDetour(&p, nullptr);
    CHECK(unity_calls == before && render_calls == 1);
    g_session.profile = &kCharacters[2];
    effect_name = "jsspsi";
    throw_effect = true;
    PlayRenderVfxDetour(&p, nullptr);
    CHECK(render_calls == 2 && g_session.features_failed);
    throw_effect = false;
    g_session = {};
    g_pose_data_profile = &kCharacters[1];
    g_pose_data_ready = true;
    CHECK(!BeginPoseOverlay(&visual_identity, &kCharacters[2]) && g_pose_data_profile == &kCharacters[1] &&
          g_pose_data_ready);
    // Every optional method is checked before installation, independently of Liino.
    host.create_hook = HookFake;
    host.resolve_class = ClassFake;
    auto resolve = [] {
        for (int i = 0; i < MethodCount; ++i) {
            g_methods[i].resolved.method_info = reinterpret_cast<void *>(uintptr_t(i + 1));
            g_methods[i].resolved.method_pointer = reinterpret_cast<void *>(uintptr_t(i + 1));
        }
    };
    for (int id = JsspsiTmpAnimator; id < MethodCount; ++id) {
        resolve();
        bad_static = id;
        hook_calls = 0;
        InitializeJsspsiContracts(FlagsFake);
        CHECK(!g_methods[id].resolved.method_info);
    }
    resolve();
    bad_static = -1;
    InitializeJsspsiContracts(FlagsFake);
    CHECK(g_methods[JsspsiPostEvent].resolved.method_info && g_methods[JsspsiStopPlaying].resolved.method_info);
    for (int id : {EffectActionPlay, VisualPerformId, VisualPerformEntity, EntityValid, GetCharCtrl, IsMain}) {
        resolve();
        g_methods[id].resolved = {};
        InitializeJsspsiContracts(FlagsFake);
        CHECK(!g_ending_splash_skip_contract);
        resolve();
        bad_static = id;
        InitializeJsspsiContracts(FlagsFake);
        CHECK(!g_ending_splash_skip_contract);
        bad_static = -1;
    }
    for (int id : {VisualActionHandle, VisualActionData, ActionIndex}) {
        resolve();
        auto field = g_fields[id].resolved;
        g_fields[id].resolved = {};
        InitializeJsspsiContracts(FlagsFake);
        CHECK(!g_ending_splash_skip_contract);
        g_fields[id].resolved = field;
    }
    resolve();
    InitializeJsspsiContracts(FlagsFake);
    CHECK(g_ending_splash_skip_contract);
    TestConfigurationRelevance();
    g_session = {};
    g_host = nullptr;

    std::puts(
        "Jsspsi: configuration/defaults, unknown keys ignored, perform suffix, ending-splash suppression, "
        "capture/main-character exclusions, actor fast path, lifetime/VFX clocks, mesh hide and sound stop passed.");
}
