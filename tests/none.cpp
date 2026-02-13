#include <bozo/core/none.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

using namespace testing;

TEST(none_t, should_be_equal_to_none_t_object) {
    EXPECT_EQ(bozo::none_t{}, bozo::none_t{});
}

TEST(none_t, should_be_not_equal_to_any_other_type_object) {
    EXPECT_NE(bozo::none, int{});
    EXPECT_NE(bozo::none, double{});
    EXPECT_NE(bozo::none, std::string{});
}

TEST(none_t, should_return_void_when_called) {
    EXPECT_TRUE(std::is_void_v<decltype(bozo::none(1, 2, "str"))>);
}

TEST(none_t, should_return_void_when_applied) {
    EXPECT_TRUE(std::is_void_v<decltype(bozo::none_t::apply(1, 2, "str"))>);
}

TEST(IsNone, should_return_true_for_none_t) {
    EXPECT_TRUE(bozo::IsNone<bozo::none_t>);
}

TEST(IsNone, should_return_false_for_different_type) {
    EXPECT_FALSE(bozo::IsNone<int>);
    EXPECT_FALSE(bozo::IsNone<class other>);
}

} // namespace
