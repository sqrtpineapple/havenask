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
#ifndef ASYNC_SIMPLE_EXECUTOR_CREATOR_MACRO_H
#define ASYNC_SIMPLE_EXECUTOR_CREATOR_MACRO_H


#include <functional>
#include "Config.h"
#include "async_simple/Common.h"
#include "async_simple/Executor.h"
#include "async_simple/IOExecutor.h"
#include "async_simple/util/Condition.h"
#include <string>
#include <chrono>
#include <thread>


#if ASYNC_SIMPLE_COROUTINE_AVAILABLE
#include "async_simple/experimental/coroutine.h"
#endif

namespace async_simple {

#if ASYNC_SIMPLE_COROUTINE_AVAILABLE
constexpr CurrentExecutor GetCurrentExecutor() { return {}; }
#endif

}

#include "ExecutorCreator.h"

/*
  添加一种新的 Executor 实现，需要完成以下2个步骤:
  1.在你的 Executor cpp 内调用 REGISTER_ASYNC_SIMPLE_EXECUTOR(type) 的宏，
  例如， REGISTER_ASYNC_SIMPLE_EXECUTOR(simple) { return new SimpleExecutor(params); }
  params 是 async_simple::ExecutorCreator::Parameters 结构，用来传递参数
  2.在该文件对应的目标BUILD目标上添加 alwayslink = True, 可参考 SimpleExecutor 和对应的BUILD文件

  如果想确保某种类型的 Executor 一定被注册，可以使用 CHECK_ASYNC_SIMPL_EXECUTOR 来检测，如果未链接会编译失败
  这两个宏都需要在 global namesapce 使用

*/

#define REGISTER_ASYNC_SIMPLE_EXECUTOR(TYPE)                                                                            \
    class Executor##TYPE##Creator : public ::async_simple::detail::ExecutorCreatorBase {                                \
    public:                                                                                                            \
        Executor##TYPE##Creator() = default;                                                                           \
        ~Executor##TYPE##Creator() {}                                                                                  \
        std::unique_ptr<async_simple::Executor> Create(const Parameters &param) override;                               \
    };                                                                                                                 \
    bool Executor##TYPE##Creator_dummy = false;                                                                        \
    __attribute__((constructor(102))) void Register##Executor##TYPE##Creator() {                                       \
        ::async_simple::detail::ExecutorCreatorBase::Register(#TYPE, std::make_unique<Executor##TYPE##Creator>());      \
    }                                                                                                                  \
    std::unique_ptr<async_simple::Executor> Executor##TYPE##Creator::Create(const Parameters &params)

#define CHECK_ASYNC_SIMPLE_EXECUTOR_OLD(TYPE)                                                                           \
    extern bool Executor##TYPE##Creator_dummy;                                                                         \
    namespace {                                                                                                        \
    __attribute__((constructor(103))) void Check##TYPE##Executor() {                                                   \
        ::Executor##TYPE##Creator_dummy = false;                                                                       \
        if (!async_simple::ExecutorCreator::HasExecutor(#TYPE)) {                                                       \
            std::fprintf(stdout, "executor " #TYPE " not registed, LINK it to your bazel target");                     \
            ::fflush(stdout);                                                                                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    }                                                                                                                  \
    }

#define CHECK_ASYNC_SIMPLE_EXECUTOR(TYPE)                                                                               \
    extern bool Executor##TYPE##Creator_dummy;                                                                         \
    namespace {                                                                                                        \
    __attribute__((constructor(103))) void Check##TYPE##Executor() { ::Executor##TYPE##Creator_dummy = false; }        \
    }

#endif // ASYNC_SIMPLE_EXECUTOR_CREATOR_MACRO_H
