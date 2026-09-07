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
#ifndef ASYNC_SIMPLE_CORO_TRACE_H
#define ASYNC_SIMPLE_CORO_TRACE_H

#include "async_simple/Common.h"
#include "Log.h"
#include "async_simple/Executor.h"
#include "Executor.h"
#include "async_simple/coro/Lazy.h"
#include "alog/Logger.h"
#include "alog/Configurator.h"
#include <exception>

namespace async_simple {
namespace coro {

struct Tracer {
    FL_LOG_DECLARE();
};

}  // namespace coro
}  // namespace async_simple

#define FL_TRACE(executor, format, args...)                                                 \
    if (async_simple::coro::Tracer::_logger->isLevelEnabled(alog::LOG_LEVEL_TRACE1)) {       \
        size_t _fl_coro_trace_contextId = executor ? executor->currentContextId() : 0;      \
        async_simple::coro::Tracer::_logger->log(alog::LOG_LEVEL_TRACE1, __FILE__, __LINE__, \
                                                __FUNCTION__, "[CoroTrace][%lu] " format,   \
                                                _fl_coro_trace_contextId, ##args);          \
    }

#define FL_CORO_TRACE(format, args...)                                                      \
    if (async_simple::coro::Tracer::_logger->isLevelEnabled(alog::LOG_LEVEL_TRACE1)) {       \
        auto _fl_coro_trace_executor = co_await async_simple::CurrentExecutor();             \
        size_t _fl_coro_trace_contextId =                                                   \
            _fl_coro_trace_executor ? _fl_coro_trace_executor->currentContextId() : 0;      \
        async_simple::coro::Tracer::_logger->log(alog::LOG_LEVEL_TRACE1, __FILE__, __LINE__, \
                                                __FUNCTION__, "[CoroTrace][%lu] " format,   \
                                                _fl_coro_trace_contextId, ##args);          \
    }



#endif
