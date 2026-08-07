#include <gtest/gtest.h>

#include "executor/JoinStateStore.hpp"

using namespace nexusflow;
using namespace nexusflow::executor;

namespace {

Message MakeTaggedMessage(int value, std::uint64_t timestampMs) {
    Message message(value);
    message.MetaData().timestamp = timestampMs;
    return message;
}

TEST(JoinStateStoreTest, TakeCompleteInputs_ReturnsExpectedPortsInRequestedOrder) {
    JoinStateStore store;

    store.Insert(7, "right", MakeTaggedMessage(2, 200));
    store.Insert(7, "left", MakeTaggedMessage(1, 100));

    std::vector<PortMessage> inputs;
    ASSERT_TRUE(store.TakeCompleteInputs({"left", "right"}, inputs));
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(inputs[0].port, "left");
    EXPECT_EQ(inputs[1].port, "right");
    EXPECT_EQ(inputs[0].message.Borrow<int>(), 1);
    EXPECT_EQ(inputs[1].message.Borrow<int>(), 2);
    EXPECT_EQ(store.PendingGroupCount(), 0u);
}

TEST(JoinStateStoreTest, SweepAndTakeCompleteInputs_EvictsExpiredGroupsBeforeReturningCompleteGroup) {
    JoinStateStore store;

    store.Insert(1, "left", MakeTaggedMessage(10, 100));
    store.Insert(2, "left", MakeTaggedMessage(20, 200));
    store.Insert(2, "right", MakeTaggedMessage(30, 210));

    std::vector<PortMessage> inputs;
    const auto result = store.SweepAndTakeCompleteInputs({"left", "right"}, 250, 120, 8, inputs);

    EXPECT_EQ(result.expiredGroupCount, 1u);
    EXPECT_EQ(result.overflowGroupCount, 0u);
    EXPECT_TRUE(result.tookCompleteGroup);
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(inputs[0].message.Borrow<int>(), 20);
    EXPECT_EQ(inputs[1].message.Borrow<int>(), 30);
    EXPECT_EQ(store.PendingGroupCount(), 0u);
}

TEST(JoinStateStoreTest, SweepAndTakeCompleteInputs_EnforcesLimitBeforeReturningInput) {
    JoinStateStore store;

    store.Insert(1, "left", MakeTaggedMessage(10, 100));
    store.Insert(2, "left", MakeTaggedMessage(20, 200));
    store.Insert(2, "right", MakeTaggedMessage(30, 200));

    std::vector<PortMessage> inputs;
    const auto result = store.SweepAndTakeCompleteInputs({"left", "right"}, 220, 1000, 1, inputs);

    EXPECT_EQ(result.expiredGroupCount, 0u);
    EXPECT_EQ(result.overflowGroupCount, 1u);
    EXPECT_TRUE(result.tookCompleteGroup);
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(inputs[0].message.Borrow<int>(), 20);
    EXPECT_EQ(inputs[1].message.Borrow<int>(), 30);
    EXPECT_EQ(store.PendingGroupCount(), 0u);
}

} // namespace
