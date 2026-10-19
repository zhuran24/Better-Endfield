// Exception isolation is scoped to the dash that entered the callback.
void FailDashFeatures() noexcept;
template<class F> void GuardDashFeatures(F&& work) noexcept {
    std::shared_ptr<DashPoseContext> context;
    void* handle = nullptr;
    try {
        std::lock_guard<std::recursive_mutex> lock(g_feature_mutex);
        context = g_dash_pose; handle = g_session.handle;
        if (!g_stopping && !g_session.features_failed) work();
    } catch (...) {
        // A nested native callback may already have replaced the failed dash.
        std::lock_guard<std::recursive_mutex> lock(g_feature_mutex);
        if (context == g_dash_pose && handle == g_session.handle) FailDashFeatures();
    }
}
