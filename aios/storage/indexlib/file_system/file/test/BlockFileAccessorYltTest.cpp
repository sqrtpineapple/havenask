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

#include "indexlib/file_system/file/BlockFileAccessor.h"

#include <memory>
#include <string>

#include "autil/EnvUtil.h"
#include "async_simple/executors/YltIOContextExecutor.h"
#include "indexlib/file_system/fslib/FslibWrapper.h"
#include "indexlib/file_system/fslib/YltFslibFileWrapper.h"
#include "indexlib/util/FutureExecutor.h"
#include "indexlib/util/cache/BlockCacheCreator.h"
#include "indexlib/util/testutil/unittest.h"

namespace indexlib::file_system {

class BlockFileAccessorYltTest : public INDEXLIB_TESTBASE
{
public:
    void TestOpenAndRead()
    {
        autil::EnvGuard envGuard("INDEXLIB_USE_IO_URING", "true");
        std::unique_ptr<async_simple::Executor, void (*)(async_simple::Executor*)> executor(
            util::FutureExecutor::CreateExecutor(1, 32), util::FutureExecutor::DestroyExecutor);
        ASSERT_NE(nullptr, dynamic_cast<async_simple::executors::YltIOContextExecutor*>(executor.get()));

        const std::string filePath = GET_TEMP_DATA_PATH() + "/block_file_accessor_ylt";
        ASSERT_EQ(FSEC_OK, FslibWrapper::AtomicStore(filePath, "0123456789").Code());

        auto cacheOption = util::BlockCacheOption::LRU(4096 * 4, 4096, 4);
        std::unique_ptr<util::BlockCache> blockCache(util::BlockCacheCreator::Create(cacheOption));
        ASSERT_NE(nullptr, blockCache);

        BlockFileAccessor accessor(blockCache.get(), false, false, "");
        accessor._executor = executor.get();
        ASSERT_EQ(FSEC_OK, accessor.Open(filePath, 10).Code());
        ASSERT_NE(nullptr, dynamic_cast<YltFslibFileWrapper*>(accessor._filePtr.get()));

        char buffer[5] = {};
        auto result = accessor.ReadAsync(buffer, 4, 3, ReadOption()).get();
        ASSERT_EQ(FSEC_OK, result.Code());
        ASSERT_EQ(4, result.Value());
        ASSERT_EQ("3456", std::string(buffer, 4));
    }
};

INDEXLIB_UNIT_TEST_CASE(BlockFileAccessorYltTest, TestOpenAndRead);

} // namespace indexlib::file_system
