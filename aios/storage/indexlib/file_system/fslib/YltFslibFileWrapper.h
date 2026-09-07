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

#include "indexlib/file_system/fslib/FslibCommonFileWrapper.h"

namespace indexlib::file_system {

class YltFslibFileWrapper final : public FslibCommonFileWrapper
{
public:
    YltFslibFileWrapper(fslib::fs::File* file, bool useDirectIO = false, bool needClose = true);
    ~YltFslibFileWrapper() override;

    YltFslibFileWrapper(const YltFslibFileWrapper&) = delete;
    YltFslibFileWrapper& operator=(const YltFslibFileWrapper&) = delete;

    FSResult<void> Open(async_simple::Executor* executor) noexcept;
    FSResult<void> Close() noexcept override;
    bool IsOpen() const noexcept;

    async_simple::Future<FSResult<size_t>> PReadAsync(void* buffer, size_t length, off_t offset, int advice,
                                                     async_simple::Executor* executor) noexcept override;
    async_simple::Future<FSResult<size_t>> PReadVAsync(const iovec* iov, int iovcnt, off_t offset, int advice,
                                                      async_simple::Executor* executor,
                                                      int64_t timeout) noexcept override;
    async_simple::coro::Lazy<FSResult<size_t>> PReadAsync(void* buffer, size_t length, off_t offset, int advice,
                                                         int64_t timeout) noexcept override;
    async_simple::coro::Lazy<FSResult<size_t>> PReadVAsync(const iovec* iov, int iovcnt, off_t offset, int advice,
                                                          int64_t timeout) noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;

private:
    AUTIL_LOG_DECLARE();
};

using YltFslibFileWrapperPtr = std::shared_ptr<YltFslibFileWrapper>;

} // namespace indexlib::file_system
