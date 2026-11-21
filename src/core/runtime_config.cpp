#include "cube/runtime_config.hpp"

#include <algorithm>
#include <atomic>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cube {
namespace {

std::atomic<int> g_profile{static_cast<int>(PerformanceProfile::Balanced)};

} // namespace

void set_performance_profile(PerformanceProfile profile) noexcept {
    g_profile.store(static_cast<int>(profile), std::memory_order_relaxed);
}

PerformanceProfile performance_profile() noexcept {
    const int v = g_profile.load(std::memory_order_relaxed);
    if (v == static_cast<int>(PerformanceProfile::Quiet)) return PerformanceProfile::Quiet;
    if (v == static_cast<int>(PerformanceProfile::Full)) return PerformanceProfile::Full;
    return PerformanceProfile::Balanced;
}

std::size_t recommended_worker_count(bool memory_bound) noexcept {
    const unsigned raw_hw = std::thread::hardware_concurrency();
    const std::size_t hw = raw_hw ? static_cast<std::size_t>(raw_hw) : 1;
    if (hw <= 1) return 1;
    switch (performance_profile()) {
    case PerformanceProfile::Quiet:
        return std::clamp<std::size_t>(hw / 2, 1, 12);
    case PerformanceProfile::Balanced: {
        const std::size_t cap = memory_bound ? 20 : 16;
        const std::size_t keep = hw <= 4 ? 1 : 2;
        return std::clamp<std::size_t>(hw > keep ? hw - keep : 1, 1, cap);
    }
    case PerformanceProfile::Full:
        return std::clamp<std::size_t>(hw, 1, memory_bound ? 24 : 20);
    }
    return std::clamp<std::size_t>(hw > 2 ? hw - 2 : 1, 1, 16);
}

void apply_worker_thread_priority() noexcept {
#ifdef _WIN32
    const auto p = performance_profile();
    if (p == PerformanceProfile::Quiet)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    else if (p == PerformanceProfile::Balanced)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

std::uint64_t total_physical_ram_bytes() noexcept {
#ifdef _WIN32
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) return static_cast<std::uint64_t>(st.ullTotalPhys);
#endif
    return 16ull * 1024ull * 1024ull * 1024ull;
}

std::uint64_t available_physical_ram_bytes() noexcept {
#ifdef _WIN32
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) return static_cast<std::uint64_t>(st.ullAvailPhys);
#endif
    return 8ull * 1024ull * 1024ull * 1024ull;
}

std::size_t pattern_cache_budget_bytes() noexcept {
    const auto total = total_physical_ram_bytes();
    const auto avail = available_physical_ram_bytes();
    constexpr std::uint64_t GiB = 1024ull * 1024ull * 1024ull;
    if (total < 10 * GiB || avail < 2 * GiB) return 128u * 1024u * 1024u;
    if (total < 20 * GiB || avail < 4 * GiB) return 256u * 1024u * 1024u;
    return 512u * 1024u * 1024u;
}

std::size_t pattern_high_gen_entry_limit() noexcept {
    const auto total = total_physical_ram_bytes();
    const auto avail = available_physical_ram_bytes();
    constexpr std::uint64_t GiB = 1024ull * 1024ull * 1024ull;
    if (total < 12 * GiB || avail < 3 * GiB) return 3'500'000u;
    return 18'000'000u;
}

} // namespace cube
