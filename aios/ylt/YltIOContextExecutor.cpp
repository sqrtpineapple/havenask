/*
 * Copyright 2014-present Alibaba Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "YltIOContextExecutor.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "Executor.h"
#include "async_simple/Signal.h"
#include "asio/dispatch.hpp"
#include "asio/io_context.hpp"
#include "asio/steady_timer.hpp"
#include "ylt/coro_io/io_context_pool.hpp"

namespace async_simple::executors {

YltIOContextExecutor::YltIOContextExecutor(size_t threadNum, bool cpuAffinity, std::string name)
    : Executor(std::move(name))
    , _pool(std::make_unique<coro_io::io_context_pool>(threadNum, cpuAffinity))
{
    const auto poolSize = _pool->pool_size();
    _innerExecutors.reserve(poolSize);
    _contexts.reserve(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
        auto* executor = _pool->get_executor();
        _innerExecutors.push_back(executor);
        _contexts.push_back(executor->checkout());
    }

    _poolThread = std::thread([this]() { _pool->run(); });
}

YltIOContextExecutor::~YltIOContextExecutor()
{
    _pool->stop();
    if (_poolThread.joinable()) {
        _poolThread.join();
    }
}

bool YltIOContextExecutor::schedule(Func func)
{
    auto* executor = getNextInnerExecutor();
    return executor != nullptr && executor->schedule(std::move(func));
}

bool YltIOContextExecutor::schedule(Func func, uint64_t scheduleInfo)
{
    auto* executor = getNextInnerExecutor();
    return executor != nullptr && executor->schedule(std::move(func), scheduleInfo);
}

bool YltIOContextExecutor::currentThreadInExecutor() const
{
    return std::any_of(_innerExecutors.begin(), _innerExecutors.end(),
                       [](const auto* executor) { return executor->currentThreadInExecutor(); });
}

ExecutorStat YltIOContextExecutor::stat() const { return {}; }

size_t YltIOContextExecutor::currentContextId() const
{
    for (const auto* executor : _innerExecutors) {
        if (executor->currentThreadInExecutor()) {
            return executor->currentContextId();
        }
    }
    return 0;
}

YltIOContextExecutor::Context YltIOContextExecutor::checkout()
{
    for (auto* executor : _innerExecutors) {
        if (executor->currentThreadInExecutor()) {
            return executor->checkout();
        }
    }
    return NULLCTX;
}

bool YltIOContextExecutor::checkin(Func func, Context ctx)
{
    return checkin(std::move(func), ctx, ScheduleOptions());
}

bool YltIOContextExecutor::checkin(Func func, Context ctx, ScheduleOptions)
{
    const auto index = findContext(ctx);
    return index != _contexts.size() && _innerExecutors[index]->checkin(std::move(func), ctx);
}

IOExecutor* YltIOContextExecutor::getIOExecutor() { return nullptr; }

void YltIOContextExecutor::schedule(Func func, Duration dur)
{
    schedule(std::move(func), dur, static_cast<uint64_t>(Priority::DEFAULT), nullptr);
}

void YltIOContextExecutor::schedule(Func func, Duration dur, uint64_t, Slot* slot)
{
    auto* executor = getNextInnerExecutor();
    if (executor == nullptr) {
        return;
    }

    auto* ioContext = static_cast<asio::io_context*>(executor->checkout());
    auto timer = std::make_shared<std::pair<asio::steady_timer, std::atomic<bool>>>(
        asio::steady_timer(*ioContext, dur), false);
    if (slot == nullptr) {
        timer->first.async_wait([func = std::move(func), timer](const auto&) mutable { func(); });
        return;
    }

    if (!signalHelper{SignalType::Terminate}.tryEmplace(slot, [timer](auto, auto*) mutable {
            bool expected = false;
            if (!timer->second.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                timer->first.cancel();
            }
        })) {
        asio::dispatch(timer->first.get_executor(), std::move(func));
        return;
    }

    timer->first.async_wait([func = std::move(func), timer](const auto&) mutable { func(); });
    bool expected = false;
    if (!timer->second.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        timer->first.cancel();
    }
}

Executor* YltIOContextExecutor::getNextInnerExecutor() noexcept
{
    return _pool == nullptr ? nullptr : _pool->get_executor();
}

size_t YltIOContextExecutor::findContext(Context ctx) const noexcept
{
    const auto iterator = std::find(_contexts.begin(), _contexts.end(), ctx);
    return static_cast<size_t>(std::distance(_contexts.begin(), iterator));
}

} // namespace async_simple::executors

REGISTER_ASYNC_SIMPLE_EXECUTOR(ylt_io) {
    const auto threadNum = params.GetThreadNum().value_or(1);
    const auto cpuAffinity = params.Get<bool>("cpu_affinity").value_or(false);
    auto name = params.GetExecutorName().value_or("ylt_io");
    return std::make_unique<async_simple::executors::YltIOContextExecutor>(threadNum, cpuAffinity, std::move(name));
}
