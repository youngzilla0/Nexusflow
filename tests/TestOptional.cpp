/**
 * @file OptionalTest.cpp
 * @brief Unit tests for Optional<T>
 */

#include "common/Optional.hpp"
#include <gtest/gtest.h>

namespace {
namespace nf = nexusflow;

using IntOpt = nf::Optional<int>;
using StrOpt = nf::Optional<std::string>;

TEST(OptionalTest, DefaultConstructor) {
    IntOpt opt;
    EXPECT_FALSE(opt);
    EXPECT_FALSE(opt.hasValue());
}

TEST(OptionalTest, NulloptConstructor) {
    IntOpt opt(nf::nullopt);
    EXPECT_FALSE(opt);
    EXPECT_FALSE(opt.hasValue());
}

TEST(OptionalTest, ValueConstructor) {
    IntOpt opt(42);
    ASSERT_TRUE(opt);
    ASSERT_TRUE(opt.hasValue());
    EXPECT_EQ(opt.value(), 42);
    EXPECT_EQ(*opt, 42);
}

TEST(OptionalTest, CopyConstructor) {
    IntOpt opt1(100);
    IntOpt opt2(opt1);
    ASSERT_TRUE(opt2);
    EXPECT_EQ(opt2.value(), 100);
}

TEST(OptionalTest, MoveConstructor) {
    StrOpt opt1(std::string("hello"));
    StrOpt opt2(std::move(opt1));
    ASSERT_TRUE(opt2);
    EXPECT_EQ(opt2.value(), "hello");
}

TEST(OptionalTest, CopyAssignment) {
    IntOpt opt1(200);
    IntOpt opt2;
    opt2 = opt1;
    ASSERT_TRUE(opt2);
    EXPECT_EQ(opt2.value(), 200);
}

TEST(OptionalTest, MoveAssignment) {
    StrOpt opt1(std::string("world"));
    StrOpt opt2;
    opt2 = std::move(opt1);
    ASSERT_TRUE(opt2);
    EXPECT_EQ(opt2.value(), "world");
}

TEST(OptionalTest, Reset) {
    IntOpt opt(999);
    ASSERT_TRUE(opt);
    opt.reset();
    EXPECT_FALSE(opt);
    EXPECT_FALSE(opt.hasValue());
}

TEST(OptionalTest, ValueOr) {
    IntOpt empty;
    IntOpt filled(77);
    EXPECT_EQ(empty.value_or(0), 0);
    EXPECT_EQ(filled.value_or(0), 77);
}

TEST(OptionalTest, OperatorArrow) {
    struct Foo {
        int x = 123;
    };
    nf::Optional<Foo> opt((Foo()));
    ASSERT_TRUE(opt);
    EXPECT_EQ(opt->x, 123);
}

TEST(OptionalTest, ExceptionOnEmpty) {
    IntOpt opt;
    EXPECT_THROW(opt.value(), std::runtime_error);
}

} // anonymous namespace