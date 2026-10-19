// Fault injection at the production managed-call boundary; no copied lifecycle.
void RunLifecycleRegressions() {
    // Off-thread entry must reject before dereferencing any session-owned pointer.
    Reset(false);
    live_thread = 2;
    g_session.profile = reinterpret_cast<const CharacterProfile*>(uintptr_t(1));
    CHECK(!DashFeaturesActive());
    g_session.profile = Profile();
    live_thread = 1;
    // A failed acquire must stay failed for this dash even after the foreign writer leaves.
    Reset(false);
    lease_host.available = false;
    CHECK(!AcquireDashRoot(g_dash_pose, 0, &root));
    lease_host.available = true;
    CHECK(!AcquireDashRoot(g_dash_pose, 0, &root));
    CHECK(lease_host.acquires == 1 && !lease_host.token);

    // The last companion bind and the early accessory pass can both reenter shutdown.
    for (bool early : {true, false}) {
        auto config = Reset(early);
        companions_available = !early;
        bool fired = false;
        int transforms = 0;
        on_method = [&](MethodId id) {
            if (fired)
                return;
            if ((early && id == GetAnimator) || (!early && id == PoseComponentTransform && ++transforms == 4)) {
                fired = true;
                Shutdown();
            }
        };
        SeamFixFrame(&component_token, early);
        on_method = {};
        CHECK(fired && !g_seam && !g_dash_pose && !pin_count && !tail.writes && !spare.writes);
    }

    // A pose getter may preserve the lease while replacing its root or detaching a bone.
    for (const std::string change : {"destroy", "reparent", "root", "config"}) {
        Reset(false);
        SeamReady();
        live_frame.time = kJsspsiLoop.begin;
        on_pose_read = [&] {
            if (change == "destroy")
                tail.alive = false;
            if (change == "reparent")
                tail.child = false;
            if (change == "root")
                live_root = &spare;
            if (change == "config")
                Configure("schema_version=3\nenabled=false\n");
        };
        SeamFixFrame(&component_token);
        on_pose_read = {};
        CHECK(tail.writes == 0);
        if (change == "root" || change == "config")
            CHECK(spare.writes == 0);
    }

    // Cancel hands the exact token to the fade; a nested real Start acquires afresh.
    for (bool nested : {false, true}) {
        auto config = Reset();
        config->jsspsi_seam_fix = false;
        Rewound();
        Tick();
        const auto token = g_accessory->root_lease->token;
        FinishDash();
        CHECK(g_accessory->fading && lease_host.token == token);
        bool fired = false;
        if (nested) {
            on_pose_read = [&] {
                if (!fired) {
                    fired = true;
                    NewStart();
                }
            };
            FadeTick(.05);
            on_pose_read = {};
            CHECK(fired);
        } else
            NewStart();
        CHECK(g_accessory && !g_accessory->fading && g_dash_pose);
        g_session.policy.active = true;
        Rewound();
        ++live_frame_id;
        const auto before = tail.writes;
        Tick();
        CHECK(tail.writes == before + 1 && g_accessory->root_lease->token != token && lease_host.acquires == 2);
        FinishDash(false);
        CHECK(!pin_count && !lease_host.token);
    }

    // An old frame's exception must not tear down the new session it reentered.
    auto config = Reset();
    config->jsspsi_seam_fix = false;
    bool replaced = false;
    on_method = [&](MethodId id) {
        if (id != GetAnimator || replaced)
            return;
        replaced = true;
        NewStart();
        throw std::bad_alloc();
    };
    GuardDashFeatures([&] { AccessoryPrepareFrame(&component_token); });
    on_method = {};
    CHECK(replaced && g_dash_pose && g_accessory && !g_session.features_failed);

    // Native Start cancellation cannot manufacture a fresh fade inside that Start.
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    g_start = [](void *, int, const void *) {
        Cancel("native start reentry", false, false);
        CHECK(!g_accessory);
        Configure("schema_version=3\nenabled=false\n");
    };
    StartDetour(&component_token, 0, nullptr);
    CHECK(!g_accessory && !pin_count);

    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    FinishDash();
    ReleaseDetour(&component_token, false, nullptr);
    CHECK(!g_accessory && !pin_count && !lease_host.token);

    // Recording owns a snapshot until the in-flight getter returns from Shutdown.
    for (MethodId fault : {ClipName, PoseGetLocal}) {
        config = Reset(false);
        config->jsspsi_capture = true;
        bool stopped = false;
        if (fault == PoseGetLocal)
            CaptureBegin(&component_token, Profile());
        on_method = [&](MethodId id) {
            if (id == fault && !stopped) {
                stopped = true;
                Shutdown();
            }
        };
        if (fault == ClipName)
            CaptureBegin(&component_token, Profile());
        else
            CaptureFrame(&component_token);
        on_method = {};
        CHECK(stopped && !g_capture && !pin_count);
    }
    // Each field boundary must stop before dereferencing an invalidated request.
    for (bool apply : {false, true}) for (const std::string invalidation : {"lease", "shutdown", "configuration"}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        Rewound(); Tick(); ++live_frame_id;
        if (apply) AccessoryPrepareFrame(&component_token);
        bool invalidated = false;
        unsigned stale_handle_reads = 0;
        const auto writes = tail.writes + spare.writes;
        on_field = [&](FieldId id) {
            if (id == Request && !invalidated) {
                invalidated = true;
                if (invalidation == "shutdown") Shutdown();
                else if (invalidation == "configuration") Configure("schema_version=3\nenabled=false\n");
                else lease_host.token = 0;
            } else if (id == RequestHandle && invalidated) ++stale_handle_reads;
        };
        if (apply) AccessoryApplyFrame(&component_token);
        else AccessoryPrepareFrame(&component_token);
        on_field = {};
        if (invalidation == "configuration")
            std::printf("Field config invalidation: apply=%d request=%d subsequent_reads=%u writes=%u\n",
                apply, invalidated, stale_handle_reads, tail.writes + spare.writes - writes);
        CHECK(invalidated && stale_handle_reads == 0 && tail.writes + spare.writes == writes);
    }

    // A post-dash failure flags the idle session; legacy Start must clear it too.
    config = Reset(); config->jsspsi_seam_fix = false;
    Rewound(); Tick(); FinishDash();
    on_pose_read = [] { throw std::bad_alloc(); };
    GuardDashFeatures([&] { FadeTick(.05); });
    on_pose_read = {};
    CHECK(!g_session.component && g_session.features_failed && !g_accessory && !pin_count);
    template_id = kCharacters[0].template_id;
    perform_name = kCharacters[0].perform_id;
    requested_handle = &next_handle_token;
    StartDetour(&component_token, 0, nullptr);
    CHECK(g_session.component == &component_token && g_session.profile == &kCharacters[0]);
    CHECK(!g_session.features_failed && HoldContext(&component_token));
    template_id = Profile()->template_id;
    perform_name = std::string(Profile()->perform_id) + "R";

    // TailLate may observe a new configuration before PreLate or a hold hook.
    for (int role : {0, 1}) {
        config = Reset(false);
        config->external_loop = true;
        g_session.profile = &kCharacters[role];
        g_pose_owner.component = &component_token;
        g_pose_owner.animator = g_pose_owner.root = &root;
        g_pose_owner.clock.state = PoseClock::State::Playing;
        g_pose_owner.clock.side = 1;
        g_pose_owner.clock.weight = 1;
        auto next = std::make_shared<Configuration>(*config);
        next->jsspsi_accessory_fade = .8f;
        g_config.store(next);
        ApplyPoseOverlay(&component_token, .016f);
        CHECK(g_session.config == next && g_pose_owner.clock.state == PoseClock::State::Playing);
    }
    Cleanup();
    std::puts("Lifecycle: Cancel/Start handoff, failed-acquire latch, reentrant seam/capture and old-frame exceptions "
              "passed.");
}

// The native tick still runs once, but no retired frame may update a new dash.
unsigned rewind_native_ticks = 0;
void __fastcall RewindNativeTick(void*, float, const void*) { ++rewind_native_ticks; }
void __fastcall RewindVfxTick(void*, float, const void*) {}
void RunRewindReentryTests() {
    for (const std::string point : {"rider", "mount", "sound", "freeze", "side"}) {
        for (int restart : {0, 1, 2}) {
            auto config = Reset(false);
            config->jsspsi_seam_fix = false;
            companions_available = true;
            for (auto& effect : g_session.effects) effect.disabled = true;
            g_pre_late = RewindNativeTick;
            g_vfx_tick = RewindVfxTick;
            rewind_native_ticks = 0;
            live_frame.time = .32f;
            if (point == "side") g_session.loop.c.begin = .8f; // Only the side window wraps.
            g_session.dash_sound_playing = 77;
            bool fired = false;
            unsigned crossfades = 0, calls_after_reentry = 0;
            auto retire = [&] {
                if (fired) return;
                fired = true;
                Cancel("managed rewind reentry", false, false);
                if (restart) {
                    // Exercise both a new handle and reuse of the old managed handle.
                    requested_handle = restart == 1 ? &next_handle_token : &handle_token;
                    StartDetour(&component_token, 0, nullptr);
                    CHECK(g_session.component && g_dash_pose && !g_session.features_failed);
                    g_session.dash_sound_playing = 991;
                    g_session.dash_sound_event = 992;
                    g_session.vfx_count = 1;
                    g_session.vfx_helpers[0] = &spare;
                    g_session.vfx_helper_count = 1;
                    g_session.vfx_frozen = false;
                    for (auto& tick : g_session.side_rewind_tick) tick = 12345;
                }
                calls_after_reentry = managed_calls;
            };
            on_method = [&](MethodId id) {
                if (fired) return;
                if (id == CrossFade) {
                    ++crossfades;
                    if ((point == "mount" && crossfades == 2) || point == "side" || point == "rider") retire();
                }
                if (point == "sound" && id == JsspsiStopPlaying) retire();
            };
            on_field = [&](FieldId id) {
                if (point == "freeze" && id == JsspsiRenderHelper) retire();
            };
            PreLateDetour(&component_token, .016f, nullptr);
            on_method = {}; on_field = {};
            CHECK(fired && rewind_native_ticks == 1 && managed_calls == calls_after_reentry);
            if (restart) {
                CHECK(g_session.component == &component_token && g_session.handle == requested_handle);
                CHECK(!g_session.policy.active && !g_session.rewound && !g_session.animation_replays);
                CHECK(g_session.last_special_hash == 0 && g_session.policy.pending_time == 0);
                CHECK(g_session.dash_sound_playing == 991 && g_session.dash_sound_event == 992);
                CHECK(g_session.vfx_count == 1 && g_session.vfx_helper_count == 1 && !g_session.vfx_frozen);
                for (const auto tick : g_session.side_rewind_tick) CHECK(tick == 12345);
            } else CHECK(!g_session.component && !g_session.config);
            Cleanup(); CHECK(!pin_count);
            std::printf("Rewind reentry: point=%s restart=%d old_frame_stopped=1\n", point.c_str(), restart);
        }
    }
}
