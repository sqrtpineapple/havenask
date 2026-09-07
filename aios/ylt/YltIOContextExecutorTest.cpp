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

#include <chrono>
#include <future>
#include <thread>

#include "ExecutorCreator.h"
#include "async_simple/Executor.h"
#include "async_simple/Try.h"
#include "async_simple/coro/Lazy.h"
#include "gtest/gtest.h"

namespace async_simple::executors {
namespace {

using namespace std::chrono_literals;

struct ExecutionContext {
    Executor::Context context;
    size_t contextId;
    std::thread::id threadId;
    bool inExecutor;
};

coro::Lazy<bool> resumeTimerOnSameContext(Executor* executor)
{
    const auto contextId = executor->currentContextId();
    co_await executor->after(1ms);
    co_return executor->currentThreadInExecutor() && executor->currentContextId() == contextId;
}

TEST(YltIOContextExecutorTest, ScheduleAndCheckin)
{
    ExecutorCreator::Parameters params;
    params.SetThreadNum(2).SetExecutorName("test_ylt_io");
    auto executor = ExecutorCreator::Create("ylt_io", params);
    ASSERT_NE(nullptr, executor);
    EXPECT_EQ("test_ylt_io", executor->name());

    std::promise<ExecutionContext> firstPromise;
    auto firstFuture = firstPromise.get_future();
    ASSERT_TRUE(executor->schedule([&]() {
        firstPromise.set_value(
            {executor->checkout(), executor->currentContextId(), std::this_thread::get_id(),
             executor->currentThreadInExecutor()});
    }));
    ASSERT_EQ(std::future_status::ready, firstFuture.wait_for(5s));
    const auto first = firstFuture.get();
    ASSERT_TRUE(first.inExecutor);
    ASSERT_NE(Executor::NULLCTX, first.context);

    std::promise<ExecutionContext> secondPromise;
    auto secondFuture = secondPromise.get_future();
    ASSERT_TRUE(executor->checkin(
        [&]() {
            secondPromise.set_value(
                {executor->checkout(), executor->currentContextId(), std::this_thread::get_id(),
                 executor->currentThreadInExecutor()});
        },
        first.context));
    ASSERT_EQ(std::future_status::ready, secondFuture.wait_for(5s));
    const auto second = secondFuture.get();
    EXPECT_TRUE(second.inExecutor);
    EXPECT_EQ(first.context, second.context);
    EXPECT_EQ(first.contextId, second.contextId);
    EXPECT_EQ(first.threadId, second.threadId);
}

TEST(YltIOContextExecutorTest, TimerResumesOnExecutor)
{
    ExecutorCreator::Parameters params;
    auto executor = ExecutorCreator::Create("ylt_io", params);
    ASSERT_NE(nullptr, executor);

    std::promise<bool> promise;
    auto future = promise.get_future();
    std::move(resumeTimerOnSameContext(executor.get()))
        .via(executor.get())
        .start([&](Try<bool>&& result) { promise.set_value(result.value()); });

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(future.get());
}

} // namespace
} // namespace async_simple::executors
