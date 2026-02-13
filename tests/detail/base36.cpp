#include <bozo/detail/base36.h>

#include <gtest/gtest.h>

namespace {

TEST(b36tol, should_with_HV001_return_29999809) {
    EXPECT_EQ(bozo::detail::b36tol("HV001"), 29999809);
}

TEST(b36tol, should_with_hv001_return_29999809) {
    EXPECT_EQ(bozo::detail::b36tol("hv001"), 29999809);
}

TEST(ltob36, should_with_29999809_return_HV001) {
    EXPECT_EQ("HV001", bozo::detail::ltob36(29999809));
}

} // namespace
