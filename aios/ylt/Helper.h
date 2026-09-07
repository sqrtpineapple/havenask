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
#ifndef ASYNC_SIMPLE_HELPER_H
#define ASYNC_SIMPLE_HELPER_H


#include "async_simple/Common.h"
#include "async_simple/Try.h"
#include "async_simple/Future.h"
#include "async_simple/Promise.h"
#include <exception>
#include <iterator>
#include <vector>

#include <iostream>

namespace async_simple {

// return Future<vector<Try<T>>>
template <typename Iter>
inline Future<std::vector<Try<typename std::iterator_traits<Iter>::value_type::
value_type>>>
collectAll(Iter begin, Iter end)
{
    using T = typename std::iterator_traits<Iter>::value_type::value_type;
    size_t n = std::distance(begin, end);

    bool allReady = true;
    for (auto iter = begin; iter != end; ++iter)
    {
        if (!iter->hasResult())
        {
            allReady = false;
            break;
        }
    }
    if (allReady)
    {
        std::vector<Try<T>> results;
        results.reserve(n);
        for (auto iter = begin; iter != end; ++iter)
        {
            results.push_back(std::move(iter->result()));
        }
        return Future<std::vector<Try<T>>>(std::move(results));
    }

    Promise<std::vector<Try<T>>> promise;
    auto future = promise.getFuture();

    struct Context {
        Context(size_t n, Promise<std::vector<Try<T>>> p_)
            : results(n)
            , p(std::move(p_))
        {
        }
        ~Context()
        {
            p.setValue(std::move(results));
        }
        std::vector<Try<T>> results;
        Promise<std::vector<Try<T>>> p;
    };

    auto ctx = std::make_shared<Context>(n, std::move(promise));
    for (size_t i = 0; i < n; ++i) {
        auto cur = begin + i;
        if (cur->hasResult()) {
            ctx->results[i] = std::move(cur->result());
        } else {
            cur->setContinuation([ctx, i](Try<T>&& t) mutable {
                ctx->results[i] = std::move(t);
            });
        }
    }
    return future;
}

// makeReadyFuture functions are already defined in async_simple/Future.h
}

#endif //ASYNC_SIMPLE_HELPER_H
