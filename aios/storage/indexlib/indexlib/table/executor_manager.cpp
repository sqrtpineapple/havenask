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
#include "indexlib/table/executor_manager.h"

#include <cstddef>
#include <type_traits>
#include <utility>

#include "alog/Logger.h"
#include "async_simple/Executor.h"
#include "indexlib/table/executor_provider.h"

using namespace std;

namespace indexlib { namespace table {
IE_LOG_SETUP(table, ExecutorManager);

ExecutorManager::ExecutorManager() {}

ExecutorManager::~ExecutorManager() { mExecutors.clear(); }

std::shared_ptr<async_simple::Executor>
ExecutorManager::RegisterExecutor(const std::shared_ptr<ExecutorProvider>& provider)
{
    const string& executorName = provider->GetExecutorName();
    auto it = mExecutors.find(executorName);
    if (it != mExecutors.end()) {
        return it->second;
    }
    {
        autil::ScopedLock lock(mMapLock);
        async_simple::Executor* executor = provider->CreateExecutor();
        if (!executor) {
            IE_LOG(ERROR, "create async_simple::executor[%s] failed", executorName.c_str());
            return nullptr;
        }
        auto it = mExecutors.insert(make_pair(executorName, std::shared_ptr<async_simple::Executor>(executor))).first;
        return it->second;
    }
}

size_t ExecutorManager::ClearUselessExecutors()
{
    autil::ScopedLock lock(mMapLock);
    size_t counter = 0;
    for (auto it = mExecutors.begin(); it != mExecutors.end();) {
        if (it->second.use_count() == 1) {
            it = mExecutors.erase(it);
            counter++;
        } else {
            ++it;
        }
    }
    return counter;
}
}} // namespace indexlib::table
