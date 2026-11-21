#pragma once

#include <cstddef>
#include <cstdint>

namespace cube {

enum class PerformanceProfile {
    Quiet,    // ~50% logical cores, below-normal thread priority
    Balanced, // Default: leaves 1-2 cores for OS/UI, below-normal priority
    Full      // Maximum throughput across available cores
};

void set_performance_profile(PerformanceProfile profile) noexcept;
[[nodiscard]] PerformanceProfile performance_profile() noexcept;

// Recommended worker thread count for search/table-build pools.
// memory_bound=true allows slightly higher concurrency (SMT hides DRAM latency).
[[nodiscard]] std::size_t recommended_worker_count(bool memory_bound = false) noexcept;

// Applies OS thread priority for background solver workers (below-normal in Quiet/Balanced).
void apply_worker_thread_priority() noexcept;

// Physical RAM helpers for adaptive pruning-table budgets.
[[nodiscard]] std::uint64_t total_physical_ram_bytes() noexcept;
[[nodiscard]] std::uint64_t available_physical_ram_bytes() noexcept;
[[nodiscard]] std::size_t pattern_cache_budget_bytes() noexcept;
[[nodiscard]] std::size_t pattern_high_gen_entry_limit() noexcept;

} // namespace cube
