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
#include "indexlib/file_system/fslib/YltFslibFileWrapper.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <ios>
#include <mutex>
#include <string_view>
#include <sys/uio.h>
#include <utility>

#include "alog/Logger.h"
#include "async_simple/Promise.h"
#include "async_simple/Try.h"
#include "async_simple/executors/YltIOContextExecutor.h"
#include "fslib/fs/File.h"
#include "ylt/coro_io/coro_file.hpp"

namespace indexlib::file_system {
namespace {

ErrorCode ParseYltError(const std::error_code& error) noexcept
{
    if (!error) {
        return FSEC_OK;
    }
    switch (error.value()) {
    case EBADF:
    case EINVAL:
        return FSEC_BADARGS;
    case EISDIR:
        return FSEC_ISDIR;
    case ENOENT:
        return FSEC_NOENT;
    case ENOTSUP:
        return FSEC_NOTSUP;
    case ETIMEDOUT:
        return FSEC_OPERATIONTIMEOUT;
    case EEXIST:
        return FSEC_EXIST;
    default:
        return FSEC_ERROR;
    }
}

std::string NormalizeLocalPath(std::string path)
{
    constexpr std::string_view localPrefix = "LOCAL://";
    if (path.compare(0, localPrefix.size(), localPrefix) == 0) {
        path.erase(0, localPrefix.size());
    }
    return path;
}

} // namespace

AUTIL_LOG_SETUP(indexlib.file_system, YltFslibFileWrapper);

class YltFslibFileWrapper::Impl
{
public:
    explicit Impl(std::string filePath, bool useDirectIO)
        : filePath(std::move(filePath)), useDirectIO(useDirectIO)
    {
    }

    std::string filePath;
    bool useDirectIO;
    mutable std::mutex mutex;
    std::shared_ptr<coro_io::random_coro_file> asyncFile;
};

YltFslibFileWrapper::YltFslibFileWrapper(fslib::fs::File* file, bool useDirectIO, bool needClose)
    : FslibCommonFileWrapper(file, useDirectIO, needClose)
    , _impl(std::make_unique<Impl>(file == nullptr ? std::string() : file->getFileName(), useDirectIO))
{
}

YltFslibFileWrapper::~YltFslibFileWrapper()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->asyncFile) {
        _impl->asyncFile->close();
        _impl->asyncFile.reset();
    }
}

FSResult<void> YltFslibFileWrapper::Open(async_simple::Executor* executor) noexcept
{
    auto* yltExecutor = dynamic_cast<async_simple::executors::YltIOContextExecutor*>(executor);
    if (yltExecutor == nullptr || _file == nullptr || !_file->isOpened()) {
        return FSEC_BADARGS;
    }

    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->asyncFile) {
        return FSEC_OK;
    }

    const auto filePath = NormalizeLocalPath(_impl->filePath);
    if (filePath.empty() || filePath.find("://") != std::string::npos) {
        return FSEC_NOTSUP;
    }

    try {
        auto* innerExecutor = yltExecutor->getNextInnerExecutor();
        auto* executorWrapper = dynamic_cast<coro_io::ExecutorWrapper<>*>(innerExecutor);
        if (executorWrapper == nullptr) {
            return FSEC_BADARGS;
        }

        auto asyncFile = std::make_shared<coro_io::random_coro_file>(executorWrapper);
        if (!asyncFile->open(filePath, std::ios::in | std::ios::binary, _impl->useDirectIO)) {
            AUTIL_LOG(ERROR, "failed to open YLT file [%s]: %s", _impl->filePath.c_str(), std::strerror(errno));
            return FSEC_ERROR;
        }
        _impl->asyncFile = std::move(asyncFile);
        return FSEC_OK;
    } catch (const std::exception& exception) {
        AUTIL_LOG(ERROR, "failed to open YLT file [%s]: %s", _impl->filePath.c_str(), exception.what());
        return FSEC_ERROR;
    }
}

FSResult<void> YltFslibFileWrapper::Close() noexcept
{
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (_impl->asyncFile) {
            _impl->asyncFile->close();
            _impl->asyncFile.reset();
        }
    }
    return FslibCommonFileWrapper::Close();
}

bool YltFslibFileWrapper::IsOpen() const noexcept
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->asyncFile != nullptr && _impl->asyncFile->is_open();
}

async_simple::Future<FSResult<size_t>>
YltFslibFileWrapper::PReadAsync(void* buffer, size_t length, off_t offset, int advice,
                                async_simple::Executor* executor) noexcept
{
    if (executor == nullptr) {
        return FslibCommonFileWrapper::PReadAsync(buffer, length, offset, advice, executor);
    }

    auto promise = std::make_shared<async_simple::Promise<FSResult<size_t>>>();
    auto future = promise->getFuture();
    future.setExecutor(executor);
    if (executor->currentThreadInExecutor() && executor->checkout() != async_simple::Executor::NULLCTX) {
        promise->checkout();
        promise->forceSched();
    }
    std::move(PReadAsync(buffer, length, offset, advice, -1))
        .via(executor)
        .start([promise](async_simple::Try<FSResult<size_t>>&& result) mutable {
            if (result.hasError()) {
                promise->setException(result.getException());
            } else {
                promise->setValue(std::move(result).value());
            }
        });
    return future;
}

async_simple::Future<FSResult<size_t>>
YltFslibFileWrapper::PReadVAsync(const iovec* iov, int iovcnt, off_t offset, int advice,
                                 async_simple::Executor* executor, int64_t timeout) noexcept
{
    if (executor == nullptr) {
        return FslibCommonFileWrapper::PReadVAsync(iov, iovcnt, offset, advice, executor, timeout);
    }

    auto promise = std::make_shared<async_simple::Promise<FSResult<size_t>>>();
    auto future = promise->getFuture();
    future.setExecutor(executor);
    if (executor->currentThreadInExecutor() && executor->checkout() != async_simple::Executor::NULLCTX) {
        promise->checkout();
        promise->forceSched();
    }
    std::move(PReadVAsync(iov, iovcnt, offset, advice, timeout))
        .via(executor)
        .start([promise](async_simple::Try<FSResult<size_t>>&& result) mutable {
            if (result.hasError()) {
                promise->setException(result.getException());
            } else {
                promise->setValue(std::move(result).value());
            }
        });
    return future;
}

async_simple::coro::Lazy<FSResult<size_t>>
YltFslibFileWrapper::PReadAsync(void* buffer, size_t length, off_t offset, int, int64_t) noexcept
{
    if (offset < 0 || (buffer == nullptr && length != 0)) {
        co_return FSResult<size_t>(FSEC_BADARGS, 0);
    }
    if (length == 0) {
        co_return FSResult<size_t>(FSEC_OK, 0);
    }

    std::shared_ptr<coro_io::random_coro_file> asyncFile;
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        asyncFile = _impl->asyncFile;
    }
    if (!asyncFile || !asyncFile->is_open()) {
        co_return FSResult<size_t>(FSEC_ERROR, 0);
    }

    auto [error, readLength] =
        co_await asyncFile->async_read_at(static_cast<uint64_t>(offset), static_cast<char*>(buffer), length);
    co_return error ? FSResult<size_t>(ParseYltError(error), 0) : FSResult<size_t>(FSEC_OK, readLength);
}

async_simple::coro::Lazy<FSResult<size_t>>
YltFslibFileWrapper::PReadVAsync(const iovec* iov, int iovcnt, off_t offset, int advice, int64_t timeout) noexcept
{
    if (iovcnt < 0 || offset < 0 || (iov == nullptr && iovcnt != 0)) {
        co_return FSResult<size_t>(FSEC_BADARGS, 0);
    }

    size_t totalReadLength = 0;
    auto currentOffset = offset;
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_base == nullptr && iov[i].iov_len != 0) {
            co_return FSResult<size_t>(FSEC_BADARGS, 0);
        }
        auto result = co_await PReadAsync(iov[i].iov_base, iov[i].iov_len, currentOffset, advice, timeout);
        if (!result.OK()) {
            co_return result;
        }
        totalReadLength += result.Value();
        if (result.Value() != iov[i].iov_len) {
            break;
        }
        currentOffset += static_cast<off_t>(iov[i].iov_len);
    }
    co_return FSResult<size_t>(FSEC_OK, totalReadLength);
}

} // namespace indexlib::file_system
