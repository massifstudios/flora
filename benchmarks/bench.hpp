#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace flora::bench {

// Minimal deterministic-workload benchmark harness (ARCHITECTURE.md §13).
// Workloads are fixed-size and seeded; only the measured wall time varies
// between runs, so results are comparable across revisions.

struct Result {
    std::string name;
    std::uint64_t operations = 0;
    double totalNanoseconds = 0.0;
    std::string operationLabel;
};

class Registry {
public:
    template <typename WorkFn>
    void run(const std::string& name, std::uint64_t operations, const std::string& operationLabel,
             WorkFn&& work) {
        using Clock = std::chrono::steady_clock;

        std::forward<WorkFn>(work)(); // warm-up: touch memory, fault pages

        constexpr int kRepeats = 3; // best-of to shed scheduler noise
        double best = 1.0e30;
        for (int repeat = 0; repeat < kRepeats; ++repeat) {
            const auto start = Clock::now();
            std::forward<WorkFn>(work)();
            const auto stop = Clock::now();
            const double nanoseconds =
                std::chrono::duration<double, std::nano>(stop - start).count();
            if (nanoseconds < best) {
                best = nanoseconds;
            }
        }
        results_.push_back({name, operations, best, operationLabel});
    }

    void report() const {
        std::printf("\n%-44s %14s %12s %16s\n", "benchmark", "total ms", "ns/op", "ops/s");
        std::printf("%s\n", std::string(90, '-').c_str());
        for (const Result& result : results_) {
            const double perOp = result.totalNanoseconds / static_cast<double>(result.operations);
            const double opsPerSecond = 1.0e9 / perOp;
            std::printf("%-44s %14.3f %12.2f %16.0f  (%s)\n", result.name.c_str(),
                        result.totalNanoseconds / 1.0e6, perOp, opsPerSecond,
                        result.operationLabel.c_str());
        }
    }

private:
    std::vector<Result> results_;
};

} // namespace flora::bench
