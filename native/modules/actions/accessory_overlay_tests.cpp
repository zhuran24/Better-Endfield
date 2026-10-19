#include "accessory_test_host.h"
using namespace AccessoryTest;
struct Scenario {
    const char *name;
    void (*run)(std::shared_ptr<Configuration>);
};
void TestNoEntryEarly(std::shared_ptr<Configuration> config) {

    config->jsspsi_seam_fix = true;
    live_hash = 528430122;
    live_frame.time = .315f;
    auto table = std::make_shared<AccessoryTable>(*g_accessory_table);
    table->entries.resize(1);
    g_accessory->table = table;
    SeamReady();
    SeamFixFrame(g_session.component, true);
    CHECK(tail.writes == 1 && spare.writes == 1 && Near(tail.pose.position.x, .5));
}
void TestFadeReentry(std::shared_ptr<Configuration>) {

    Tick();
    FinishDash();
    unsigned depth = 0;
    on_method = [&](MethodId method) {
        if (method == GetAnimator && ++depth < 5)
            AccessoryFadeFrame(&component_token);
    };
    FadeTick(.05);
    on_method = {};
    CHECK(depth == 2 && g_accessory->fade_logged);
}
void TestShutdownSeam(std::shared_ptr<Configuration> config) {

    config->jsspsi_seam_fix = true;
    SeamReady();
    live_frame.time = .315f;
    g_stopping = true;
    SeamFixFrame(g_session.component);
    CHECK(tail.writes == 0 && spare.writes == 0);
}
void TestShutdownRead(std::shared_ptr<Configuration> config) {

    config->jsspsi_seam_fix = true;
    SeamReady();
    live_frame.time = .315f;
    on_pose_read = [] { g_stopping = true; };
    SeamFixFrame(g_session.component);
    on_pose_read = {};
    CHECK(tail.writes == 0 && spare.writes == 0);
}
void TestApplyLeaseReentry(std::shared_ptr<Configuration>) {

    AccessoryPrepareFrame(g_session.component);
    on_method = [](MethodId method) {
        if (method == PoseIsChild)
            lease_host.token = 0;
    };
    AccessoryApplyFrame(g_session.component);
    on_method = {};
    CHECK(tail.writes == 0);
}
void TestClaimLeaseLost(std::shared_ptr<Configuration>) {

    AccessoryPrepareFrame(g_session.component);
    lease_host.token = 0;
    CHECK(!AccessoryClaimsSnapshot(g_session.component).contains(&tail));
    CHECK(!AccessoryDefersSeam(g_session.component));
}
void TestEarlyGuards(std::shared_ptr<Configuration> config) {

    for (const std::string veto : {"table", "mask", "window", "ambiguous", "lease", "bone", "pin"}) {
        config = Reset();
        SeamReady();
        live_frame.time = .315f;
        if (veto == "table")
            g_accessory->table.reset();
        if (veto == "mask")
            config->jsspsi_accessory_mask = 0;
        if (veto == "window")
            g_session.loop.c.begin = .32f;
        if (veto == "ambiguous") {
            auto table = std::make_shared<AccessoryTable>(*g_accessory_table);
            table->entries[1] = table->entries[0];
            g_accessory->table = table;
        }
        if (veto == "lease") {
            lease_host.available = false;
            lease_host.token = 0;
        }
        if (veto == "bone")
            tail.child = false;
        if (veto == "pin")
            pin_fail_after = 0;
        SeamFixFrame(g_session.component, true);
        CHECK(veto == "lease"  ? tail.writes == 0 && spare.writes == 0
              : veto == "bone" ? tail.writes == 0 && spare.writes == 1
                               : tail.writes == 1 && spare.writes == 1);
        const auto writes = tail.writes;
        Tick();
        CHECK(tail.writes == writes);
    }
    config = Reset();
    SeamReady();
    live_frame.time = .315f;
    SeamFixFrame(g_session.component, true);
    CHECK(tail.writes == 0 && spare.writes == 0);
    Tick();
    CHECK(tail.writes == 0 && spare.writes == 1);
    Rewound();
    ++live_frame_id;
    live_frame.time = .14f;
    SeamFixFrame(g_session.component, true);
    live_frame.time = .25f;
    Tick();
    CHECK(Near(g_accessory->snapshot.time, .25));
}
void TestFadeEdges(std::shared_ptr<Configuration> config) {

    for (const std::string veto : {"blocked", "unbound", "no_held", "lease"}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        Rewound();
        Tick();
        if (veto == "blocked")
            g_accessory->blocked = true;
        if (veto == "unbound")
            g_accessory->bound = false;
        if (veto == "no_held")
            for (auto &bone : g_accessory->bindings)
                bone.held_valid = false;
        if (veto == "lease")
            lease_host.token = 0;
        FinishDash();
        CHECK(!g_accessory && !pin_count);
    }
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    FinishDash();
    const auto writes = tail.writes, calls = managed_calls;
    real_seconds = 10.05;
    AccessoryFadeFrame(&spare);
    CHECK(g_accessory && tail.writes == writes && managed_calls == calls);
    real_seconds = 10.3;
    AccessoryFadeFrame(&spare);
    CHECK(!g_accessory && !pin_count && !lease_host.token);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    FinishDash();
    const auto start = g_accessory->fade_start;
    real_seconds = 10.05;
    CancelAccessory(true);
    CHECK(g_accessory->fade_start == start);
    auto next = std::make_shared<Configuration>(*config);
    next->jsspsi_accessory_fade = .8f;
    g_config.store(next);
    FadeTick(.201);
    CHECK(!g_accessory);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    auto shared = AcquireDashRoot(g_dash_pose, 0, &root);
    const auto token = shared->token;
    Tick();
    CHECK(g_accessory->root_lease == shared && lease_host.acquires == 1);
    shared.reset();
    FinishDash();
    CHECK(g_accessory && lease_host.token == token);
    FadeTick(.201);
    CHECK(!g_accessory && !pin_count && lease_host.releases == 1);
}
void TestFadeSetterReentry(std::shared_ptr<Configuration> config) {

    for (const std::string change : {"cancel", "new_owner", "shutdown", "lease"}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        config->jsspsi_accessory_mask = 0x80000001u;
        spare.name = "bone_1";
        Rewound();
        Tick();
        FinishDash();
        const auto first = tail.writes, second = spare.writes;
        on_pose_write = [&] {
            CHECK(pin_count > 0);
            if (change == "cancel")
                EndAccessory();
            if (change == "shutdown")
                g_stopping = true;
            if (change == "lease")
                lease_host.token = 0;
            if (change == "new_owner") {
                NewStart();
            }
        };
        FadeTick(.05);
        on_pose_write = {};
        CHECK(tail.writes == first + 1 && spare.writes == second);
        CHECK(change == "new_owner" ? g_accessory && !g_accessory->fading : !g_accessory);
    }
}
void TestSnapshot(std::shared_ptr<Configuration>) {

    live_frame.transitioning = true;
    live_frame.outgoing = true;
    Tick();
    live_frame.transitioning = false;
    live_frame.outgoing = false;
    ++live_frame_id;
    Tick();
    CHECK(tail.writes == 1);
}
void TestReparent(std::shared_ptr<Configuration>) {

    AccessoryPrepareFrame(g_session.component);
    tail.child = false;
    AccessoryApplyFrame(g_session.component);
    CHECK(tail.writes == 0);
}
void TestRootDestroyed(std::shared_ptr<Configuration>) {

    AccessoryPrepareFrame(g_session.component);
    root.alive = false;
    AccessoryApplyFrame(g_session.component);
    CHECK(tail.writes == 0);
}
void TestHandleChanged(std::shared_ptr<Configuration>) {

    AccessoryPrepareFrame(g_session.component);
    requested_handle = nullptr;
    AccessoryApplyFrame(g_session.component);
    CHECK(tail.writes == 0);
}
void TestCaptureLoad(std::shared_ptr<Configuration> config) {

    EndAccessory();
    config->jsspsi_capture = true;
    g_accessory_checked = false;
    BeginAccessory(g_session.component);
    CHECK(!g_accessory && !g_accessory_checked);
}
void TestLeaseBusy(std::shared_ptr<Configuration>) {

    lease_host.available = false;
    Tick();
    CHECK(tail.writes == 0);
}
void TestLeaseLost(std::shared_ptr<Configuration>) {

    AccessoryPrepareFrame(g_session.component);
    lease_host.token = 0;
    AccessoryApplyFrame(g_session.component);
    CHECK(tail.writes == 0);
}
const Scenario kScenarios[]{{"no_entry_early", TestNoEntryEarly},
                            {"fade_reentry", TestFadeReentry},
                            {"shutdown_seam", TestShutdownSeam},
                            {"shutdown_read", TestShutdownRead},
                            {"apply_lease_reentry", TestApplyLeaseReentry},
                            {"claim_lease_lost", TestClaimLeaseLost},
                            {"early_guards", TestEarlyGuards},
                            {"fade_edges", TestFadeEdges},
                            {"fade_setter_reentry", TestFadeSetterReentry},
                            {"snapshot", TestSnapshot},
                            {"reparent", TestReparent},
                            {"root_destroyed", TestRootDestroyed},
                            {"handle_changed", TestHandleChanged},
                            {"capture_load", TestCaptureLoad},
                            {"lease_busy", TestLeaseBusy},
                            {"lease_lost", TestLeaseLost}};
void RunScenario(const Scenario &scenario) {
    WriteTable();
    auto config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    scenario.run(config);
    Cleanup();
    CHECK(pin_count == 0);
}
void TestSamplingAndReservations() {

    auto config = Reset(false);
    SeamReady();
    const auto before = managed_calls;
    AccessoryPrepareFrame(g_session.component);
    AccessoryApplyFrame(g_session.component);
    CHECK(managed_calls == before && !AccessoryClaimsSnapshot(g_session.component).contains(&tail));
    const auto clocks_before = clock_calls;
    const auto pins_before = pin_count;
    const auto token_before = lease_host.token;
    AccessoryFadeFrame(g_session.component);
    CancelAccessory(true);
    StopAccessoryFade();
    CHECK(managed_calls == before && clock_calls == clocks_before && pin_count == pins_before &&
          lease_host.token == token_before);
    SeamFixFrame(g_session.component);
    CHECK(tail.writes == 1 && spare.writes == 1);
    // First traversal, phase sampling and seam reservations.
    for (float u : {.28f, .30f, .315f}) {
        config = Reset();
        SeamReady();
        live_frame.time = u;
        Tick();
        CHECK(g_seam->objects[0].mode == SeamMode::Apply && !g_session.rewound && g_accessory->frames == 0 &&
              tail.writes == 0 && spare.writes == 1 && spare.pose.position.x > 0);
        if (u == .315f)
            CHECK(Near(spare.pose.position.x, .5));
    }
    config = Reset();
    live_hash = 528430122;
    SeamReady();
    live_frame.time = .315f;
    auto right_only = std::make_shared<AccessoryTable>(*g_accessory_table);
    right_only->entries.resize(1);
    g_accessory->table = right_only;
    Tick();
    CHECK(g_accessory->blocked && !AccessoryClaimsSnapshot(g_session.component).contains(&tail) && tail.writes == 1 &&
          spare.writes == 1 && Near(tail.pose.position.x, .5));
    config = Reset();
    SeamReady();
    config->jsspsi_accessory_mask = 0;
    live_frame.time = .315f;
    Tick();
    CHECK(tail.writes == 1 && Near(tail.pose.position.x, .5));

    config = Reset();
    SeamReady();
    Tick();
    CHECK(g_accessory->frames == 0 && tail.writes == 0 && spare.writes == 1);
    Rewound();
    ++live_frame_id;
    tail.writes = spare.writes = 0;
    Tick();
    CHECK(tail.writes == 1 && spare.writes == 1 && g_accessory->frames == 1);
    CHECK(AccessoryClaimsSnapshot(g_session.component).contains(&tail) && tail.writes > 0 &&
          !AccessoryClaimsSnapshot(g_session.component).contains(&spare));
    BonePose expected;
    double phase = 0;
    CHECK(AccessoryPhase(live_frame.time, .315f, .126f, phase));
    g_accessory->entry->Sample(0, phase, expected);
    CHECK(PoseSame(tail.pose.position, expected.position));
    const auto frames = g_accessory->frames;
    Tick();
    CHECK(g_accessory->frames == frames);
    config = Reset();
    SeamReady();
    Rewound();
    AccessoryPrepareFrame(g_session.component);
    const float prepared_time = live_frame.time;
    live_frame.time = .20f;
    SeamFixFrame(g_session.component);
    AccessoryApplyFrame(g_session.component);
    CHECK(Near(spare.pose.position.x, SeamShare((prepared_time - .126f) / (.315f - .126f), false)));
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    live_frame.time = .316f;
    Tick();
    CHECK(tail.writes == 1 && Near(g_accessory->phase, (.316f - double(.126f)) / (double(.315f) - .126f) - 1));
}
void TestBindingEligibility() {
    std::shared_ptr<Configuration> config;
    config = Reset();
    config->jsspsi_capture = true;
    Rewound();
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_accessory_mask = 0;
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    CHECK(tail.writes == 0 && g_accessory->skipped == 2);
    config = Reset();
    config->jsspsi_seam_fix = false;
    tree.push_back(&duplicate);
    Rewound();
    Tick();
    CHECK(tail.writes == 0 && duplicate.writes == 0 && g_accessory->bindings.empty());
    config = Reset();
    config->jsspsi_seam_fix = false;
    auto repeated = std::make_shared<AccessoryTable>(*g_accessory_table);
    repeated->entries[0].bones[1].name = "tail_01_jnt";
    g_accessory->table = repeated;
    Rewound();
    Tick();
    CHECK(tail.writes == 0 && g_accessory->bindings.empty());
    config = Reset();
    config->jsspsi_seam_fix = false;
    repeated = std::make_shared<AccessoryTable>(*g_accessory_table);
    repeated->entries[1] = repeated->entries[0];
    g_accessory->table = repeated;
    Rewound();
    Tick();
    CHECK(tail.writes == 0 && g_accessory->blocked);
    config = Reset();
    config->jsspsi_seam_fix = false;
    tree.assign(4097, &tail);
    Rewound();
    Tick();
    CHECK(g_accessory->blocked && tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    config->jsspsi_accessory_mask = uint32_t(1) << 31;
    spare.name = "bone_1";
    Rewound();
    Tick();
    CHECK(tail.writes == 0 && spare.writes == 1);
    config = Reset();
    config->jsspsi_seam_fix = false;
    tail.name = "missing";
    Rewound();
    Tick();
    CHECK(g_accessory->bindings.empty());
    config = Reset();
    config->jsspsi_seam_fix = false;
    g_session.loop.c.begin = .32f;
    Rewound();
    Tick();
    CHECK(g_accessory->blocked && tail.writes == 0);
    g_session.loop.c.begin = .315f;
    ++live_frame_id;
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    live_hash = 123;
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    requested_handle = nullptr;
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    live_frame.transitioning = true;
    live_frame.outgoing = true;
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    g_session.policy.active = false;
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    AccessoryPrepareFrame(g_session.component);
    AccessoryApplyFrame(&spare);
    CHECK(tail.writes == 0);
    FinishDash(false);
    AccessoryApplyFrame(&component_token);
    CHECK(tail.writes == 0 && pin_count == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    AccessoryPrepareFrame(g_session.component);
    g_config.store(std::make_shared<Configuration>(*config));
    AccessoryApplyFrame(g_session.component);
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    tail.child = false;
    Tick();
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    tail.write_ok = false;
    Tick();
    CHECK(g_accessory->frames == 0 && tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    live_thread = 2;
    const auto worker_calls = managed_calls;
    Tick();
    CHECK(tail.writes == 0 && managed_calls == worker_calls);
    live_thread = 1;
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    tail.alive = false;
    Tick();
    CHECK(tail.writes == 0 && !AccessoryClaimsSnapshot(g_session.component).contains(&tail));
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    AccessoryPrepareFrame(g_session.component);
    tail.alive = false;
    AccessoryApplyFrame(g_session.component);
    CHECK(tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    pin_fail_after = 1;
    Tick();
    CHECK(g_accessory->blocked && tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    tree[1] = nullptr;
    Tick();
    CHECK(g_accessory->blocked && tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    helpers_ok = false;
    Tick();
    CHECK(g_accessory->blocked && tail.writes == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    g_pose_leases = nullptr;
    Tick();
    CHECK(g_accessory->blocked && tail.writes == 0);
}
void TestLeaseOwnership() {
    std::shared_ptr<Configuration> config;
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    auto lease = AcquireDashRoot(g_dash_pose, 0, &root);
    const auto token1 = lease->token;
    Tick();
    CHECK(tail.writes == 1 && g_accessory->root_lease == lease && lease_host.acquires == 1);
    EndAccessory();
    CHECK(lease_host.token == token1 && lease_host.releases == 0);
    lease.reset();
    Cleanup();
    CHECK(!pin_count && lease_host.releases == 1);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    live_frame.transitioning = true;
    live_frame.time = .34f;
    live_frame.next_time = .151f;
    Tick();
    double transition_phase = 0;
    AccessoryPhase(.34f, .315f, .126f, transition_phase);
    CHECK(tail.writes == 1 && Near(g_accessory->phase, transition_phase));
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    AccessoryPrepareFrame(g_session.component);
    auto retained = AccessoryOwnerSnapshot();
    EndAccessory();
    CHECK(!AccessoryCurrent(retained) && pin_count > 0);
    g_dash_pose.reset();
    retained.reset();
    CHECK(pin_count == 0);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    const auto shutdown_calls = managed_calls;
    g_stopping = true;
    live_thread = 2;
    EndAccessory();
    g_dash_pose.reset();
    CHECK(pin_count == 0 && lease_host.token == 0 && managed_calls == shutdown_calls);
    live_thread = 1;
}
void TestFadeInterpolation() {
    std::shared_ptr<Configuration> config;
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    const BonePose held = tail.pose;
    // Prepare a different table sample without applying it; fade must freeze last successful write.
    ++live_frame_id;
    live_frame.time = .18f;
    AccessoryPrepareFrame(g_session.component);
    FinishDash();
    CHECK(g_accessory && g_accessory->fading && pin_count > 0 && lease_host.token != 0);
    CHECK(PoseSame(g_accessory->bindings[0].held.position, held.position));
    for (double elapsed : {0., .05, .10, .15, .199}) {
        // Supply a fresh native pose every TailLate; no reuse of previous blended output.
        const BonePose native{{0, .2955202f, 0, .9553365f}, {float(20 + elapsed), 2, 3}};
        tail.pose = native;
        FadeTick(elapsed);
        const float x = float(1 - elapsed / double(config->jsspsi_accessory_fade));
        const float w = x * x * (3 - 2 * x);
        CHECK(Near(tail.pose.position.x, std::lerp(native.position.x, held.position.x, w)) &&
              PoseSame(tail.pose.rotation, PoseSlerp(native.rotation, held.rotation, w)));
        CHECK(PoseSame(g_accessory->bindings[0].held.position, held.position));
    }
    const auto before_duplicate = tail.writes;
    AccessoryFadeFrame(&component_token);
    CHECK(tail.writes == before_duplicate);
    const auto before_finish = tail.writes;
    FadeTick(.201);
    CHECK(!g_accessory && pin_count == 0 && lease_host.token == 0 && lease_host.releases == 1 &&
          tail.writes == before_finish);
}
void TestFadeInvalidation() {
    std::shared_ptr<Configuration> config;
    for (const std::string stop : {"new_dash",          "bone_destroyed",  "root_destroyed",     "animator_changed",
                                   "root_changed",      "entity_changed",  "entity_invalid",     "character_switched",
                                   "lease_lost",        "module_off",      "switch_off",         "fade_zero",
                                   "capture",           "capture_active",  "character_disabled", "shutdown",
                                   "component_release", "reparent",        "read_failed",        "write_failed",
                                   "clock_failed",      "clock_backwards", "frame_failed"}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        Rewound();
        Tick();
        FinishDash();
        const auto writes = tail.writes;
        if (stop == "new_dash")
            StopAccessoryFade();
        else if (stop == "bone_destroyed")
            tail.alive = false;
        else if (stop == "root_destroyed")
            root.alive = false;
        else if (stop == "animator_changed")
            live_animator = &spare;
        else if (stop == "root_changed")
            live_root = &spare;
        else if (stop == "entity_changed")
            live_entity = &spare;
        else if (stop == "entity_invalid")
            entity_valid = false;
        else if (stop == "character_switched")
            is_main = false;
        else if (stop == "lease_lost")
            lease_host.token = 0;
        else if (stop == "module_off")
            config->enabled = false;
        else if (stop == "switch_off")
            config->jsspsi_accessory_loop = false;
        else if (stop == "fade_zero")
            config->jsspsi_accessory_fade = 0;
        else if (stop == "capture")
            config->jsspsi_capture = true;
        else if (stop == "capture_active")
            (g_capture = std::make_shared<CaptureState>())->active = true;
        else if (stop == "character_disabled")
            config->characters = 0;
        else if (stop == "shutdown")
            g_stopping = true;
        else if (stop == "component_release")
            ReleaseAccessoryComponent(&component_token);
        else if (stop == "reparent")
            tail.child = false;
        else if (stop == "read_failed")
            tail.read_ok = false;
        else if (stop == "write_failed")
            tail.write_ok = false;
        else if (stop == "clock_failed")
            clock_ok = false;
        else if (stop == "frame_failed")
            frame_count_ok = false;
        FadeTick(stop == "clock_backwards" ? -.01 : .05);
        CHECK(!g_accessory && tail.writes == writes && pin_count == 0);
    }
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    FinishDash();
    const auto wrong_thread_calls = managed_calls, wrong_thread_writes = tail.writes;
    live_thread = 2;
    FadeTick(.05);
    CHECK(managed_calls == wrong_thread_calls && tail.writes == wrong_thread_writes);
    EndAccessory();
    CHECK(!pin_count && !lease_host.token);
    live_thread = 1;
}
void TestFadeReentryAndEligibility() {
    std::shared_ptr<Configuration> config;
    // Fade cancellation and managed reentry.
    for (const std::string change : {"cancel", "root", "lease", "destroy", "reparent", "new_owner"}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        Rewound();
        Tick();
        FinishDash();
        const auto writes = tail.writes;
        on_pose_read = [&] {
            if (change == "cancel")
                EndAccessory();
            else if (change == "root")
                live_root = &spare;
            else if (change == "lease")
                lease_host.token = 0;
            else if (change == "destroy")
                tail.alive = false;
            else if (change == "reparent")
                tail.child = false;
            else {
                NewStart();
            }
        };
        FadeTick(.05);
        on_pose_read = {};
        CHECK(tail.writes == writes);
        CHECK(change == "new_owner" ? bool(g_accessory) && !g_accessory->fading : !g_accessory);
    }
    config = Reset();
    config->jsspsi_seam_fix = false;
    config->jsspsi_accessory_fade = 0;
    Rewound();
    Tick();
    FinishDash();
    const auto zero_calls = managed_calls, zero_writes = tail.writes;
    FadeTick(.05);
    CHECK(!g_accessory && !pin_count && !lease_host.token && managed_calls == zero_calls && tail.writes == zero_writes);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Tick();
    FinishDash();
    CHECK(!g_accessory && !pin_count && !lease_host.token);
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    auto next_config = std::make_shared<Configuration>(*config);
    next_config->jsspsi_accessory_fade = .4f;
    g_config.store(next_config);
    FinishDash();
    CHECK(g_accessory && g_accessory->fading && g_accessory->fade_seconds == double(.4f));
    FadeTick(.05);
    CHECK(g_accessory && g_accessory->fade_logged);
    ReleaseAccessoryComponent(&spare);
    CHECK(bool(g_accessory));
    for (const std::string veto : {"off", "disabled", "capture", "capture_active", "fade_zero", "clock", "release"}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        Rewound();
        Tick();
        if (veto == "off")
            config->jsspsi_accessory_loop = false;
        else if (veto == "disabled")
            config->enabled = false;
        else if (veto == "capture")
            config->jsspsi_capture = true;
        else if (veto == "capture_active")
            (g_capture = std::make_shared<CaptureState>())->active = true;
        else if (veto == "fade_zero") {
            next_config = std::make_shared<Configuration>(*config);
            next_config->jsspsi_accessory_fade = 0;
            g_config.store(next_config);
        } else if (veto == "clock")
            clock_ok = false;
        const auto calls = managed_calls;
        FinishDash(veto != "release");
        FadeTick(.05);
        CHECK(!g_accessory && !pin_count && !lease_host.token && managed_calls == calls);
    }
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    auto shared = AcquireDashRoot(g_dash_pose, 0, &root);
    auto token = shared->token;
    Tick();
    CHECK(g_accessory->root_lease == shared && lease_host.acquires == 1);
    shared.reset();
    FinishDash();
    CHECK(g_accessory && lease_host.token == token);
    FadeTick(.201);
    CHECK(!pin_count && lease_host.releases == 1);
}
void TestTableReplacement() {
    std::shared_ptr<Configuration> config;
    // Cache replacement, rejection and retry.
    config = Reset();
    const auto bank = g_accessory_table;
    BeginAccessory(g_session.component);
    CHECK(g_accessory_table == bank);
    WriteTable(true);
    BeginAccessory(g_session.component);
    CHECK(!g_accessory_table);
    WriteTable();
    BeginAccessory(g_session.component);
    CHECK(bool(g_accessory_table));
    std::filesystem::remove(test_root / "actions/accessory_jsspsi.bin");
    BeginAccessory(g_session.component);
    CHECK(!g_accessory_table);
    Cleanup();
    CHECK(pin_count == 0);
}
void RunIntegration() {
    TestSamplingAndReservations();
    TestBindingEligibility();
    TestLeaseOwnership();
    TestFadeInterpolation();
    TestFadeInvalidation();
    TestFadeReentryAndEligibility();
    TestTableReplacement();
}
// Additional production-entry tests for shared root leases and cache failures.
int cloth_calls = 0;
void __fastcall ClothFake(void *, const void *) {
    ++cloth_calls;
}
BE_Result BE_CALL ReleaseHooksFake(void *, const char *) {
    return BE_Result_Ok;
}
void RunSafetyTests() {
    WriteTable();
    for (bool accessory : {false, true}) {
        auto config = Reset(accessory);
        lease_host.available = false;
        live_frame.time = .315f;
        Tick();
        CHECK(tail.writes == 0 && spare.writes == 0 && !lease_host.token);
        ++live_frame_id;
        lease_host.available = true;
        Tick();
        CHECK(tail.writes == 0 && spare.writes == 0);
        CHECK(lease_host.acquires == 1 && !lease_host.token); // A failed root is not retried in this dash.
    }
    auto config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Node beast1{"beast1"}, beast2{"beast2"}, beast3{"beast3"};
    auto rider = AcquireDashRoot(g_dash_pose, 0, &root);
    CHECK(AcquireDashRoot(g_dash_pose, 0, &root) == rider);
    CHECK(AcquireDashRoot(g_dash_pose, 1, &beast1));
    CHECK(AcquireDashRoot(g_dash_pose, 2, &beast2));
    CHECK(AcquireDashRoot(g_dash_pose, 3, &beast3));
    const auto token = rider->token;
    rider.reset();
    Tick();
    CHECK(lease_host.acquires == 4);
    FinishDash();
    CHECK(lease_host.token == token && lease_host.companions.size() == 3);
    FadeTick(.05);
    CHECK(lease_host.token == token && lease_host.companions.size() == 3);
    FadeTick(.201);
    CHECK(!lease_host.token && lease_host.companions.empty() && !pin_count && lease_host.releases == 4);

    config = Reset(false);
    tree.clear();
    for (int i = 0; i < 90; ++i) {
        ++live_frame_id;
        SeamFixFrame(g_session.component);
    }
    CHECK(pin_count == 1 && lease_host.acquires == 1); // Only the shared root pin survives retries.
    Cleanup();
    CHECK(!pin_count);

    config = Reset(false);
    SeamReady();
    live_frame.time = .315f;
    on_pose_read = [] { lease_host.token = 0; };
    SeamFixFrame(g_session.component);
    on_pose_read = {};
    CHECK(tail.writes == 0 && spare.writes == 0);
    config = Reset(false);
    SeamReady();
    live_frame.time = (.315f + .126f) * .5f;
    for (int i = 0; i < 680; ++i) {
        BoundBone b;
        b.transform = &spare;
        b.name = "extra";
        g_seam->objects[0].bones.push_back(std::move(b));
    }
    lease_host.owns_calls = 0;
    SeamFixFrame(g_session.component);
    CHECK(lease_host.owns_calls < 60 && !tail.writes && !spare.writes);

    config = Reset(false);
    SeamReady();
    live_frame.time = .315f;
    g_cloth_update = ClothFake;
    on_pose_read = [] { throw std::bad_alloc(); };
    ClothUpdateDetour(nullptr, nullptr);
    on_pose_read = {};
    CHECK(cloth_calls == 1 && g_session.features_failed && !g_seam && !g_dash_pose && !pin_count);
    ClothUpdateDetour(nullptr, nullptr);
    CHECK(cloth_calls == 2 && !tail.writes && !spare.writes);

    config = Reset(false);
    SeamReady();
    live_frame.time = .315f;
    host.release_module_hooks = ReleaseHooksFake;
    on_pose_read = [] { Shutdown(); };
    SeamFixFrame(g_session.component);
    on_pose_read = {};
    CHECK(!g_seam && !pin_count && !tail.writes && !spare.writes);
    g_stopping = false;
    BonePose invalid;
    invalid.position.x = NAN;
    CHECK(!PoseWriteLocal(&tail, invalid));

    config = Reset(false);
    SeamReady();
    CHECK(SaveSeamTable());
    const auto cache = SeamFile();
    auto read_bytes = [](const std::filesystem::path &file) {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto original = read_bytes(cache);
#if !defined(_WIN32)
    auto temp = cache;
    temp += ".new";
    std::filesystem::create_symlink("/dev/full", temp);
    CHECK(!SaveSeamTable());
    CHECK(!std::filesystem::exists(temp) && read_bytes(cache) == original);
#endif
    config = Reset(false);
    config->jsspsi_accessory_loop = true;
    throw_pin = true;
    GuardDashFeatures([&] { BeginAccessory(g_session.component); });
    throw_pin = false;
    CHECK(g_session.features_failed && !g_accessory && !g_dash_pose && !pin_count);
    // Only the main character's native Start terminates the active fade.
    config = Reset();
    config->jsspsi_seam_fix = false;
    Rewound();
    Tick();
    FinishDash();
    g_start = [](void *, int, const void *) { Configure("schema_version=3\nenabled=false\n"); };
    is_main = false;
    StartDetour(&component_token, 0, nullptr);
    CHECK(g_accessory && g_accessory->fading);
    g_config.store(config);
    is_main = true;
    StartDetour(&component_token, 0, nullptr);
    CHECK(!g_accessory && !pin_count);
    config = Reset(false);
    // Bone diagnostics are bounded and honor the diagnostics setting.
    for (bool diagnostics : {false, true}) {
        config = Reset();
        config->jsspsi_seam_fix = false;
        config->diagnostics = diagnostics;
        config->jsspsi_accessory_mask = UINT32_MAX;
        auto table = std::make_shared<AccessoryTable>();
        std::string error;
        CHECK(Load(Make(1, 8, 10).bytes, *table, error));
        g_accessory->table = table;
        logs.clear();
        Rewound();
        Tick();
        size_t messages = 0;
        for (const auto &line : logs)
            if (line.find("bone skipped") != std::string::npos)
                ++messages;
        CHECK(messages == (diagnostics ? 4u : 0u));
        CHECK(g_accessory->skipped == 9);
    }
    // Missing-table retries report transitions only, and honor diagnostics.
    Cleanup();
    const auto missing_file = test_root / "actions/accessory_jsspsi.bin";
    std::filesystem::remove(missing_file);
    g_accessory_table_status.clear();
    logs.clear();
    LoadAccessoryTable("accessory_jsspsi.bin", false);
    CHECK(logs.empty());
    g_accessory_table_status.clear();
    LoadAccessoryTable("accessory_jsspsi.bin", true);
    LoadAccessoryTable("accessory_jsspsi.bin", true);
    CHECK(logs.size() == 1 && logs.front().find("absent or unreadable") != std::string::npos);
    WriteTable();
    LoadAccessoryTable("accessory_jsspsi.bin", false);
    std::filesystem::remove(missing_file);
    LoadAccessoryTable("accessory_jsspsi.bin", true);
    CHECK(logs.size() == 2);

    // Failed reads remain retryable even when the next file keeps the same mtime.
    const auto file = test_root / "actions/accessory_jsspsi.bin";
    Cleanup();
    std::filesystem::remove(file);
    std::filesystem::create_directory(file);
    const auto stamp = std::filesystem::last_write_time(file);
    LoadAccessoryTable("accessory_jsspsi.bin", true);
    CHECK(!g_accessory_table && !g_accessory_checked);
    std::filesystem::remove(file);
    WriteTable();
    std::filesystem::last_write_time(file, stamp);
    LoadAccessoryTable("accessory_jsspsi.bin", true);
    CHECK(g_accessory_table && g_accessory_checked);
    Cleanup();
    CHECK(!pin_count);
}

#include "lifecycle_regressions.inl"
int main(int argc, char **argv) {
    TestDirectory directory;
    test_root = directory.path;
    std::filesystem::create_directories(test_root / "actions");
    InstallHost();
    WriteTable();
    if (argc > 1) {
        const auto scenario = std::find_if(std::begin(kScenarios), std::end(kScenarios),
                                           [&](const auto &item) { return std::string_view(item.name) == argv[1]; });
        CHECK(scenario != std::end(kScenarios));
        RunScenario(*scenario);
    } else {
        RunIntegration();
        RunSafetyTests();
        RunLifecycleRegressions();
        RunRewindReentryTests();
        for (const auto &scenario : kScenarios)
            RunScenario(scenario);
    }
    Cleanup();
    CHECK(pin_count == 0);
    std::puts("Actions accessory overlay: lifecycle, ownership, fades and all regressions passed.");
}
