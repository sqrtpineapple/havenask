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

#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <sys/uio.h>

#include "ExecutorCreator.h"
#include "async_simple/coro/SyncAwait.h"
#include "fslib/fs/File.h"
#include "fslib/fs/FileSystem.h"
#include "indexlib/file_system/fslib/FslibWrapper.h"
#include "indexlib/util/testutil/unittest.h"

namespace indexlib::file_system {

class YltFslibFileWrapperTest : public INDEXLIB_TESTBASE
{
public:
    void CaseSetUp() override
    {
        _filePath = GET_TEMP_DATA_PATH() + "/ylt_random_read";
        ASSERT_EQ(FSEC_OK, FslibWrapper::AtomicStore(_filePath, "0123456789").Code());

        async_simple::ExecutorCreator::Parameters params;
        params.SetThreadNum(2);
        _executor = async_simple::ExecutorCreator::Create("ylt_io", params);
        ASSERT_NE(nullptr, _executor);

        auto* file = fslib::fs::FileSystem::openFile(_filePath, fslib::READ);
        ASSERT_NE(nullptr, file);
        ASSERT_TRUE(file->isOpened());
        _wrapper = std::make_unique<YltFslibFileWrapper>(file);
        ASSERT_EQ(FSEC_OK, _wrapper->Open(_executor.get()).Code());
    }

    void CaseTearDown() override
    {
        ASSERT_EQ(FSEC_OK, _wrapper->Close().Code());
        _wrapper.reset();
        _executor.reset();
    }

    void TestLazyRead()
    {
        char buffer[5] = {};
        auto result = async_simple::coro::syncAwait(_wrapper->PReadAsync(buffer, 4, 3, 0, -1));
        ASSERT_EQ(FSEC_OK, result.Code());
        ASSERT_EQ(4, result.Value());
        ASSERT_EQ("3456", std::string(buffer, 4));
    }

    void TestFutureRead()
    {
        char buffer[4] = {};
        auto result = _wrapper->PReadAsync(buffer, 3, 6, 0, _executor.get()).get();
        ASSERT_EQ(FSEC_OK, result.Code());
        ASSERT_EQ(3, result.Value());
        ASSERT_EQ("678", std::string(buffer, 3));
    }

    void TestFutureReadReturnsToCallingContext()
    {
        char buffer[4] = {};
        std::promise<bool> promise;
        auto future = promise.get_future();
        ASSERT_TRUE(_executor->schedule([&]() {
            auto context = _executor->checkout();
            _wrapper->PReadAsync(buffer, 3, 1, 0, _executor.get())
                .thenValue([&, context](FSResult<size_t>&& result) {
                    promise.set_value(result.OK() && result.Value() == 3 && std::string(buffer, 3) == "123" &&
                                      _executor->checkout() == context);
                });
        }));
        ASSERT_EQ(std::future_status::ready, future.wait_for(std::chrono::seconds(5)));
        EXPECT_TRUE(future.get());
    }

    void TestVectoredRead()
    {
        char first[4] = {};
        char second[5] = {};
        iovec iov[] = {{first, 3}, {second, 4}};
        auto result = async_simple::coro::syncAwait(_wrapper->PReadVAsync(iov, 2, 2, 0, -1));
        ASSERT_EQ(FSEC_OK, result.Code());
        ASSERT_EQ(7, result.Value());
        ASSERT_EQ("234", std::string(first, 3));
        ASSERT_EQ("5678", std::string(second, 4));
    }

private:
    std::string _filePath;
    std::unique_ptr<async_simple::Executor> _executor;
    std::unique_ptr<YltFslibFileWrapper> _wrapper;
};

INDEXLIB_UNIT_TEST_CASE(YltFslibFileWrapperTest, TestLazyRead);
INDEXLIB_UNIT_TEST_CASE(YltFslibFileWrapperTest, TestFutureRead);
INDEXLIB_UNIT_TEST_CASE(YltFslibFileWrapperTest, TestFutureReadReturnsToCallingContext);
INDEXLIB_UNIT_TEST_CASE(YltFslibFileWrapperTest, TestVectoredRead);

} // namespace indexlib::file_system
