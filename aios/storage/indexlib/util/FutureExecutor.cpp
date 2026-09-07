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
#include "indexlib/util/FutureExecutor.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <unistd.h>

#include "autil/EnvUtil.h"
#include "ExecutorCreator.h"
#include "indexlib/base/Constant.h"
#include "indexlib/util/metrics/Metric.h"
#include "indexlib/util/metrics/MetricProvider.h"
#include "indexlib/util/metrics/Monitor.h"
#include "kmonitor/client/MetricType.h"

using namespace std;

// CHECK_ASYNC_SIMPLE_EXECUTOR(async_io);

namespace indexlib { namespace util {
AUTIL_LOG_SETUP(indexlib.util, FutureExecutor);

std::once_flag FutureExecutor::internalExecutorFlag;
async_simple::Executor* FutureExecutor::internalExecutor = nullptr;
std::once_flag FutureExecutor::internalBuildExecutorFlag;
async_simple::Executor* FutureExecutor::internalBuildExecutor = nullptr;
std::thread FutureExecutor::reportMetricsThread;

async_simple::Executor* FutureExecutor::CreateExecutor(int threadNum, int maxAio)
{
    static int32_t idx = 0;
    const bool useYlt = autil::EnvUtil::getEnv("INDEXLIB_USE_IO_URING", false);
    const std::string executorType = useYlt ? "ylt_io" : "async_io";
    auto params = async_simple::ExecutorCreator::Parameters()
                      .SetExecutorName(executorType + "_thread_pool_" + std::to_string(idx++))
                      .SetThreadNum(threadNum)
                      .Set<uint32_t>("max_aio", maxAio);
    auto executor = async_simple::ExecutorCreator::Create(executorType, params);
    AUTIL_LOG(INFO, "pool created[%p], type[%s], threadNum[%d], max_aio [%d]", executor.get(), executorType.c_str(),
              threadNum, maxAio);
    return executor.release();
}
void FutureExecutor::DestroyExecutor(async_simple::Executor* executor)
{
    if (executor) {
        delete executor;
    }
}
async_simple::Executor* FutureExecutor::GetInternalBuildExecutor()
{
    std::call_once(internalBuildExecutorFlag, []() {
        int threadNum = autil::EnvUtil::getEnv("INDEXLIB_INTERNAL_BUILD_THREADNUM", 1);
        int maxAio = autil::EnvUtil::getEnv("INDEXLIB_INTERNAL_BUILD_MAXAIO", 32);
        if (threadNum <= 0 || maxAio <= 0) {
            AUTIL_LOG(WARN, "threadNum[%d] or maxAio[%d] illegal, do not create internal build pool", threadNum,
                      maxAio);
            return;
        }

        internalBuildExecutor = CreateExecutor(threadNum, maxAio);
    });
    return internalBuildExecutor;
}

async_simple::Executor* FutureExecutor::GetInternalExecutor()
{
    std::call_once(internalExecutorFlag, []() {
        int threadNum = autil::EnvUtil::getEnv("INDEXLIB_INTERNAL_THREADNUM", -1);
        int queueSize = autil::EnvUtil::getEnv("INDEXLIB_INTERNAL_QUEUESIZE", 32);
        if (threadNum <= 0 || queueSize <= 0) {
            AUTIL_LOG(WARN, "threadNum[%d] or queueSize[%d] illegal, do not create internal pool", threadNum,
                      queueSize);
            return;
        }

        AUTIL_LOG(INFO, "internal pool created, threadNum[%d], queueSize/max_aio [%d]", threadNum, queueSize);
        internalExecutor = CreateExecutor(threadNum, queueSize);
    });
    return internalExecutor;
}

void FutureExecutor::SetInternalExecutor(async_simple::Executor* executor)
{
    std::call_once(internalExecutorFlag, [executor]() {
        if (executor) {
            AUTIL_LOG(INFO, "internal pool setted");
        } else {
            AUTIL_LOG(INFO, "internal pool set to null");
        }
        internalExecutor = executor;
    });
}

void FutureExecutor::SetInternalBuildExecutor(async_simple::Executor* executor)
{
    std::call_once(internalBuildExecutorFlag, [executor]() {
        if (executor) {
            AUTIL_LOG(INFO, "internal pool setted");
        } else {
            AUTIL_LOG(INFO, "internal pool set to null");
        }
        internalBuildExecutor = executor;
    });
}

bool FutureExecutor::RegisterMetricsReporter(util::MetricProviderPtr metricProvider)
{
    if (!metricProvider) {
        AUTIL_LOG(WARN, "%s", "task schedule or metrics provider is empty, do not report future executor metrics");
        return true;
    }
    int32_t sleepTime = REPORT_METRICS_INTERVAL;
    auto executor = GetInternalExecutor();
    if (!executor) {
        AUTIL_LOG(INFO, "no executor created, do not report metrics");
        return true;
    }
    reportMetricsThread = std::thread([sleepTime, metricProvider, executor]() {
        IE_DECLARE_METRIC(pendingTaskCount);
        IE_INIT_METRIC_GROUP(metricProvider, pendingTaskCount, "global/ExecutorPendingTaskCount", kmonitor::STATUS,
                             "count");
        for (;;) {
            auto stat = executor->stat();
            IE_REPORT_METRIC(pendingTaskCount, stat.pendingTaskCount);
            usleep(sleepTime);
        }
    });
    return true;
}
}} // namespace indexlib::util
