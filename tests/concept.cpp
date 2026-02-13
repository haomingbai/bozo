#include <bozo/core/concept.h>

#include <list>
#include <vector>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

TEST(ForwardIterator, should_return_true_for_iterator_type) {
    EXPECT_TRUE(bozo::ForwardIterator<std::list<int>::iterator>);
}

TEST(ForwardIterator, should_return_false_for_not_iterator_type) {
    EXPECT_FALSE(bozo::ForwardIterator<int>);
}

TEST(Iterable, should_return_true_for_iterable_type) {
    EXPECT_TRUE(bozo::Iterable<std::vector<int>>);
}

TEST(Iterable, should_return_false_for_not_iterable_type) {
    EXPECT_FALSE(bozo::Iterable<int>);
}

TEST(RawDataWritable, should_return_true_for_type_with_mutable_data_method_and_non_const_result) {
    EXPECT_TRUE(bozo::RawDataWritable<std::string>);
}

TEST(RawDataWritable, should_return_false_for_type_without_mutable_data_method_or_non_const_result) {
    EXPECT_FALSE(bozo::RawDataWritable<std::string_view>);
}

TEST(RawDataWritable, should_return_false_for_type_with_data_point_to_more_than_a_single_byte_value) {
    EXPECT_FALSE(bozo::RawDataWritable<std::wstring>);
}

TEST(RawDataWritable, should_return_true_for_type_lvalue_reference) {
    EXPECT_TRUE(bozo::RawDataWritable<std::string&>);
}

TEST(RawDataWritable, should_return_true_for_type_rvalue_reference) {
    EXPECT_TRUE(bozo::RawDataWritable<std::string&&>);
}

TEST(RawDataWritable, should_return_false_for_type_const_reference) {
    EXPECT_FALSE(bozo::RawDataWritable<const std::string&>);
}

TEST(RawDataReadable, should_return_true_for_type_with_mutable_data_method_and_non_const_result) {
    EXPECT_TRUE(bozo::RawDataReadable<std::string>);
}

TEST(RawDataReadable, should_return_true_for_type_with_const_data_method_and_const_result) {
    EXPECT_TRUE(bozo::RawDataReadable<std::string_view>);
}

TEST(RawDataReadable, should_return_false_for_type_with_data_point_to_more_than_a_single_byte_value) {
    EXPECT_FALSE(bozo::RawDataReadable<std::wstring>);
}

TEST(RawDataReadable, should_return_true_for_type_lvalue_reference) {
    EXPECT_TRUE(bozo::RawDataReadable<std::string&>);
}

TEST(RawDataReadable, should_return_true_for_type_rvalue_reference) {
    EXPECT_TRUE(bozo::RawDataReadable<std::string&&>);
}

TEST(RawDataReadable, should_return_true_for_type_const_reference) {
    EXPECT_TRUE(bozo::RawDataReadable<const std::string&>);
}

} // namespace
