#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "future_lite/CoroInterface.h"
#include "future_lite/Try.h"
#include "future_lite/executors/SimpleExecutor.h"
#include "indexlib/file_system/FileSystemDefine.h"
#include "indexlib/file_system/file/BlockFileNode.h"
#include "indexlib/file_system/file/ReadOption.h"
#include "indexlib/util/cache/BlockCacheCreator.h"

namespace {

using Clock = std::chrono::steady_clock;
using indexlib::file_system::BlockFileNode;
using indexlib::file_system::FSOpenType;
using indexlib::file_system::ReadOption;
using indexlib::util::BlockCache;
using indexlib::util::BlockCacheCreator;
using indexlib::util::BlockCacheOption;

enum class InterfaceType {
    READ,
    GET_BLOCK,
};

struct Options {
    std::string file;
    InterfaceType interfaceType = InterfaceType::READ;
    size_t concurrency = 32;
    size_t executorThreads = 32;
    size_t blockSize = 4096;
    uint64_t seed = 20260910;
    int warmupSeconds = 5;
    int durationSeconds = 30;
};

struct WorkerStats {
    uint64_t operations = 0;
    uint64_t bytes = 0;
    uint64_t errors = 0;
    uint64_t shortReads = 0;
    uint64_t latencyNanos = 0;
    uint64_t checksum = 0;
    std::vector<uint32_t> latencies;
};

void Usage(const char* program)
{
    std::cerr << "Usage: " << program
              << " --file PATH [--interface read|get-block] [--concurrency N] [--executor-threads N] [--block-size N]"
                 " [--warmup-seconds N] [--duration-seconds N] [--seed N]\n";
}

bool ParsePositive(const std::string& value, size_t* output)
{
    char* end = nullptr;
    const auto parsed = std::strtoull(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0) {
        return false;
    }
    *output = static_cast<size_t>(parsed);
    return true;
}

bool ParseArgs(int argc, char** argv, Options* options)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (index + 1 >= argc) {
            return false;
        }
        const std::string value(argv[++index]);
        size_t parsed = 0;
        if (argument == "--file") {
            options->file = value;
        } else if (argument == "--interface" && value == "read") {
            options->interfaceType = InterfaceType::READ;
        } else if (argument == "--interface" && value == "get-block") {
            options->interfaceType = InterfaceType::GET_BLOCK;
        } else if (argument == "--concurrency" && ParsePositive(value, &parsed)) {
            options->concurrency = parsed;
        } else if (argument == "--executor-threads" && ParsePositive(value, &parsed)) {
            options->executorThreads = parsed;
        } else if (argument == "--block-size" && ParsePositive(value, &parsed)) {
            options->blockSize = parsed;
        } else if (argument == "--warmup-seconds" && ParsePositive(value, &parsed)) {
            options->warmupSeconds = static_cast<int>(parsed);
        } else if (argument == "--duration-seconds" && ParsePositive(value, &parsed)) {
            options->durationSeconds = static_cast<int>(parsed);
        } else if (argument == "--seed" && ParsePositive(value, &parsed)) {
            options->seed = parsed;
        } else {
            return false;
        }
    }
    return !options->file.empty() && options->blockSize % 4096 == 0;
}

uint64_t NextRandom(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * 2685821657736338717ULL;
}

future_lite::coro::Lazy<WorkerStats> RunWorker(BlockFileNode* fileNode, size_t blockSize, size_t blockCount,
                                               InterfaceType interfaceType, Clock::time_point deadline, uint64_t seed,
                                               bool collectLatency)
{
    WorkerStats stats;
    std::vector<char> buffer(blockSize);
    uint64_t randomState = seed ? seed : 1;
    if (collectLatency) {
        stats.latencies.reserve(100000);
    }
    while (Clock::now() < deadline) {
        const size_t offset = (NextRandom(&randomState) % blockCount) * blockSize;
        const auto begin = Clock::now();
        bool success = false;
        if (interfaceType == InterfaceType::READ) {
            auto result = co_await fileNode->ReadAsyncCoro(buffer.data(), blockSize, offset, ReadOption());
            if (!result.OK()) {
                ++stats.errors;
            } else if (result.Value() != blockSize) {
                ++stats.shortReads;
            } else {
                success = true;
                stats.checksum += static_cast<unsigned char>(buffer[0]);
            }
        } else {
            auto result = co_await fileNode->GetAccessor()->GetBlockAsyncCoro(offset, ReadOption());
            if (!result.OK()) {
                ++stats.errors;
            } else {
                auto handle = std::move(result.Value());
                if (!handle.GetData() || handle.GetDataSize() < blockSize) {
                    ++stats.shortReads;
                } else {
                    success = true;
                    stats.checksum += static_cast<unsigned char>(handle.GetData()[0]);
                }
            }
        }
        const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
        ++stats.operations;
        if (!success) {
            continue;
        }
        stats.bytes += blockSize;
        stats.latencyNanos += static_cast<uint64_t>(latency);
        if (collectLatency) {
            stats.latencies.push_back(static_cast<uint32_t>(std::min<int64_t>(latency, UINT32_MAX)));
        }
    }
    co_return stats;
}

future_lite::coro::Lazy<std::vector<future_lite::Try<WorkerStats>>>
RunPhase(BlockFileNode* fileNode, size_t blockSize, size_t blockCount, size_t concurrency, int seconds, uint64_t seed,
         InterfaceType interfaceType, bool collectLatency)
{
    const auto deadline = Clock::now() + std::chrono::seconds(seconds);
    std::vector<future_lite::coro::Lazy<WorkerStats>> workers;
    workers.reserve(concurrency);
    for (size_t index = 0; index < concurrency; ++index) {
        workers.emplace_back(RunWorker(fileNode, blockSize, blockCount, interfaceType, deadline,
                                       seed + index * 0x9e3779b97f4a7c15ULL, collectLatency));
    }
    co_return co_await future_lite::coro::collectAll(std::move(workers));
}

double Percentile(const std::vector<uint32_t>& sorted, double quantile)
{
    if (sorted.empty()) {
        return 0.0;
    }
    const size_t index = static_cast<size_t>(quantile * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[index]) / 1000.0;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseArgs(argc, argv, &options)) {
        Usage(argv[0]);
        return 2;
    }

    auto cacheOption = BlockCacheOption::LRU(0, options.blockSize, 1);
    cacheOption.cacheParams["num_shard_bits"] = "0";
    std::unique_ptr<BlockCache> blockCache(BlockCacheCreator::Create(cacheOption));
    if (!blockCache) {
        std::cerr << "failed to create disabled block cache\n";
        return 3;
    }

    BlockFileNode fileNode(blockCache.get(), true, false, false, "");
    const auto openResult = fileNode.Open(options.file, options.file, FSOpenType::FSOT_CACHE, -1);
    if (openResult != indexlib::file_system::FSEC_OK) {
        std::cerr << "failed to open file: " << options.file << " error=" << static_cast<int>(openResult.Code())
                  << '\n';
        return 4;
    }
    const size_t blockCount = fileNode.GetLength() / options.blockSize;
    if (blockCount == 0) {
        std::cerr << "file is smaller than one block\n";
        return 5;
    }

    future_lite::executors::SimpleExecutor executor(options.executorThreads);
    auto warmup = future_lite::coro::syncAwait(
        RunPhase(&fileNode, options.blockSize, blockCount, options.concurrency, options.warmupSeconds, options.seed,
                 options.interfaceType, false)
            .via(&executor));
    for (const auto& result : warmup) {
        if (result.hasError()) {
            std::cerr << "warmup coroutine failed\n";
            return 6;
        }
    }

    const auto begin = Clock::now();
    auto results = future_lite::coro::syncAwait(
        RunPhase(&fileNode, options.blockSize, blockCount, options.concurrency, options.durationSeconds,
                 options.seed ^ 0xd1b54a32d192ed03ULL, options.interfaceType, true)
            .via(&executor));
    const double elapsedSeconds = std::chrono::duration<double>(Clock::now() - begin).count();

    WorkerStats total;
    for (auto& result : results) {
        if (result.hasError()) {
            ++total.errors;
            continue;
        }
        auto worker = std::move(result.value());
        total.operations += worker.operations;
        total.bytes += worker.bytes;
        total.errors += worker.errors;
        total.shortReads += worker.shortReads;
        total.latencyNanos += worker.latencyNanos;
        total.checksum += worker.checksum;
        total.latencies.insert(total.latencies.end(), worker.latencies.begin(), worker.latencies.end());
    }
    std::sort(total.latencies.begin(), total.latencies.end());

    const double iops = static_cast<double>(total.operations) / elapsedSeconds;
    const double mibPerSecond = static_cast<double>(total.bytes) / (1024.0 * 1024.0) / elapsedSeconds;
    const double meanMicros = total.operations == 0
                                  ? 0.0
                                  : static_cast<double>(total.latencyNanos) / 1000.0 /
                                        static_cast<double>(total.operations);
    const char* interfaceName = options.interfaceType == InterfaceType::READ
                                    ? "BlockFileNode::ReadAsyncCoro"
                                    : "BlockFileAccessor::GetBlockAsyncCoro";
    std::cout << std::fixed << std::setprecision(3) << "RESULT_JSON {\"interface\":\"" << interfaceName
              << "\",\"backend\":\"future_lite-posix-aio\""
              << ",\"file\":\"" << options.file << "\",\"file_size\":" << fileNode.GetLength()
              << ",\"block_size\":" << options.blockSize << ",\"direct_io\":true,\"cache_bytes\":0"
              << ",\"concurrency\":" << options.concurrency << ",\"executor_threads\":"
              << options.executorThreads << ",\"warmup_seconds\":" << options.warmupSeconds
              << ",\"duration_seconds\":" << options.durationSeconds << ",\"elapsed_seconds\":"
              << elapsedSeconds << ",\"operations\":" << total.operations << ",\"iops\":" << iops
              << ",\"mib_per_second\":" << mibPerSecond << ",\"latency_mean_us\":" << meanMicros
              << ",\"latency_p50_us\":" << Percentile(total.latencies, 0.50) << ",\"latency_p95_us\":"
              << Percentile(total.latencies, 0.95) << ",\"latency_p99_us\":"
              << Percentile(total.latencies, 0.99) << ",\"latency_p999_us\":"
              << Percentile(total.latencies, 0.999) << ",\"errors\":" << total.errors
              << ",\"short_reads\":" << total.shortReads << ",\"checksum\":" << total.checksum
              << ",\"seed\":" << options.seed << "}\n";
    return total.errors == 0 && total.shortReads == 0 ? 0 : 7;
}
