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
#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "async_simple/Executor.h"

namespace coro_io {
class io_context_pool;
}

namespace async_simple::executors {

class YltIOContextExecutor final : public Executor {
public:
    explicit YltIOContextExecutor(size_t threadNum, bool cpuAffinity = false, std::string name = "ylt_io");
    ~YltIOContextExecutor() override;

    YltIOContextExecutor(const YltIOContextExecutor&) = delete;
    YltIOContextExecutor& operator=(const YltIOContextExecutor&) = delete;

    bool schedule(Func func) override;
    bool schedule(Func func, uint64_t scheduleInfo) override;
    bool currentThreadInExecutor() const override;
    ExecutorStat stat() const override;
    size_t currentContextId() const override;
    Context checkout() override;
    bool checkin(Func func, Context ctx) override;
    bool checkin(Func func, Context ctx, ScheduleOptions opts) override;
    IOExecutor* getIOExecutor() override;
    Executor* getNextInnerExecutor() noexcept;
    const std::vector<Executor*>& getInnerExecutors() const noexcept { return _innerExecutors; }

protected:
    void schedule(Func func, Duration dur) override;
    void schedule(Func func, Duration dur, uint64_t scheduleInfo, Slot* slot = nullptr) override;

private:
    size_t findContext(Context ctx) const noexcept;

private:
    std::unique_ptr<coro_io::io_context_pool> _pool;
    std::thread _poolThread;
    std::vector<Executor*> _innerExecutors;
    std::vector<Context> _contexts;
};

} // namespace async_simple::executors
