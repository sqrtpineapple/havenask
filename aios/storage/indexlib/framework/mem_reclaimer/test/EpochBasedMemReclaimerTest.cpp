#include "indexlib/framework/mem_reclaimer/EpochBasedMemReclaimer.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include "async_simple/util/Condition.h"
#include "unittest/unittest.h"

namespace indexlibv2::framework {

class EpochBasedMemReclaimerTest : public TESTBASE
{
public:
    EpochBasedMemReclaimerTest() = default;
    ~EpochBasedMemReclaimerTest() = default;

public:
    void setUp() override;
    void tearDown() override;
};

namespace {
struct DemoItem {
    bool destroyed = false;
    DemoItem() = default;
};

void FreeDemoItem(void* addr)
{
    auto item = reinterpret_cast<DemoItem*>(addr);
    ASSERT_FALSE(item->destroyed);
    item->destroyed = true;
}

class DemoReader
{
public:
    DemoReader(std::shared_ptr<EpochBasedMemReclaimer>& memReclaimer)
    {
        _memReclaimer = memReclaimer;
        _curEpochItem = _memReclaimer->CriticalGuard();
    }

    ~DemoReader() { _memReclaimer->LeaveCritical(_curEpochItem); }

    void Retire(void* addr) { _memReclaimer->Retire(addr, &FreeDemoItem); }

private:
    std::shared_ptr<EpochBasedMemReclaimer> _memReclaimer;
    EpochBasedMemReclaimer::EpochItem* _curEpochItem;
};
} // namespace

void EpochBasedMemReclaimerTest::setUp() {}

void EpochBasedMemReclaimerTest::tearDown() {}

TEST_F(EpochBasedMemReclaimerTest, TestSimple)
{
    DemoItem item;
    ASSERT_FALSE(item.destroyed);
    auto memReclaimer = std::make_shared<EpochBasedMemReclaimer>(1, nullptr);
    {
        auto reader = std::make_shared<DemoReader>(memReclaimer);
        reader->Retire((void*)(&item));
        ASSERT_EQ(1u, memReclaimer->_retireList.size());
        memReclaimer->TryReclaim();
        ASSERT_EQ(1u, memReclaimer->_retireList.size());
        ASSERT_FALSE(item.destroyed);
    }
    memReclaimer->TryReclaim();
    ASSERT_EQ(0u, memReclaimer->_retireList.size());
    ASSERT_TRUE(item.destroyed);
}

TEST_F(EpochBasedMemReclaimerTest, TestTwoReader)
{
    DemoItem item;
    ASSERT_FALSE(item.destroyed);
    auto memReclaimer = std::make_shared<EpochBasedMemReclaimer>(1, nullptr);
    async_simple::util::Condition c00;
    async_simple::util::Condition c01;
    async_simple::util::Condition c02;
    async_simple::util::Condition c03;
    std::thread r0([&]() {
        {
            auto reader = std::make_shared<DemoReader>(memReclaimer);
            c00.release();
            c01.acquire();
        }
        c02.release();
        c03.acquire();
    });
    async_simple::util::Condition c10;
    async_simple::util::Condition c11;
    std::thread r1([&]() {
        {
            auto reader = std::make_shared<DemoReader>(memReclaimer);
        }
        c10.release();
        c11.acquire();
    });

    c00.acquire();
    c10.acquire();

    memReclaimer->Retire(&item, &FreeDemoItem);

    memReclaimer->TryReclaim();
    ASSERT_EQ(1u, memReclaimer->_retireList.size());
    ASSERT_FALSE(item.destroyed);
    c01.release();
    c02.acquire();
    memReclaimer->TryReclaim();
    ASSERT_EQ(0u, memReclaimer->_retireList.size());
    ASSERT_TRUE(item.destroyed);
    c03.release();
    c11.release();
    r0.join();
    r1.join();
}

TEST_F(EpochBasedMemReclaimerTest, TestInactiveThread)
{
    auto memReclaimer = std::make_shared<EpochBasedMemReclaimer>(1, nullptr);
    DemoItem item;
    ASSERT_FALSE(item.destroyed);
    async_simple::util::Condition c00;
    async_simple::util::Condition c01;
    std::thread r0([&]() {
        {
            auto reader = std::make_shared<DemoReader>(memReclaimer);
        }
        c00.release();
        c01.acquire();
    });
    c00.acquire();

    for (size_t i = 0; i < 100; ++i) {
        memReclaimer->IncreaseEpoch();
    }

    memReclaimer->Retire(&item, &FreeDemoItem);
    EXPECT_EQ(1u, memReclaimer->_retireList.size());

    for (size_t i = 0; i < 100; ++i) {
        memReclaimer->IncreaseEpoch();
    }
    memReclaimer->TryReclaim();

    EXPECT_EQ(0u, memReclaimer->_retireList.size());
    EXPECT_TRUE(item.destroyed);

    c01.release();
    r0.join();
}

TEST_F(EpochBasedMemReclaimerTest, TestLeaveCriticalInOtherThread)
{
    auto memReclaimer = std::make_shared<EpochBasedMemReclaimer>(1, nullptr);
    DemoItem item;
    ASSERT_FALSE(item.destroyed);
    {
        auto reader = std::make_shared<DemoReader>(memReclaimer);
        memReclaimer->Retire(&item, &FreeDemoItem);
        ASSERT_EQ(1u, memReclaimer->_retireList.size());
        memReclaimer->TryReclaim();
        ASSERT_EQ(1u, memReclaimer->_retireList.size());
        ASSERT_FALSE(item.destroyed);
        std::thread t([reader = std::move(reader)]() {});
        t.join();
    }
    memReclaimer->TryReclaim();
    ASSERT_EQ(0u, memReclaimer->_retireList.size());
    ASSERT_TRUE(item.destroyed);
}

TEST_F(EpochBasedMemReclaimerTest, TestNestedCritical)
{
    auto memReclaimer = std::make_shared<EpochBasedMemReclaimer>(1, nullptr);
    DemoItem item;
    ASSERT_FALSE(item.destroyed);
    async_simple::util::Condition c00;
    async_simple::util::Condition c01;
    async_simple::util::Condition c02;
    async_simple::util::Condition c03;
    async_simple::util::Condition c04;
    async_simple::util::Condition c05;
    std::thread r0([&]() {
        {
            auto reader = std::make_shared<DemoReader>(memReclaimer);
            {
                c00.release();
                c01.acquire();
                auto reader2 = std::make_shared<DemoReader>(memReclaimer);
            }
            c02.release();
            c03.acquire();
        }
        c04.release();
        c05.acquire();
    });

    c00.acquire();

    for (size_t i = 0; i < 100; ++i) {
        memReclaimer->IncreaseEpoch();
    }
    memReclaimer->Retire(&item, &FreeDemoItem);
    EXPECT_EQ(1u, memReclaimer->_retireList.size());
    memReclaimer->TryReclaim();
    EXPECT_EQ(1u, memReclaimer->_retireList.size());

    c01.release();
    c02.acquire();

    for (size_t i = 0; i < 100; ++i) {
        memReclaimer->IncreaseEpoch();
    }

    EXPECT_EQ(1u, memReclaimer->_retireList.size());
    memReclaimer->TryReclaim();
    EXPECT_EQ(1u, memReclaimer->_retireList.size());

    for (size_t i = 0; i < 100; ++i) {
        memReclaimer->IncreaseEpoch();
    }
    c03.release();
    c04.acquire();
    memReclaimer->TryReclaim();
    EXPECT_EQ(0u, memReclaimer->_retireList.size());
    EXPECT_TRUE(item.destroyed);

    c05.release();
    r0.join();
}
} // namespace indexlibv2::framework
