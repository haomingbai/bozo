#include <bozo/io/size_of.h>
#include <bozo/ext/std/optional.h>

#include <gtest/gtest.h>

namespace bozo::tests {

struct sized_type {};

} // namespace bozo::tests

namespace bozo {

template <>
struct size_of_impl<bozo::tests::sized_type> {
    static constexpr bozo::size_type apply(const bozo::tests::sized_type&) {
        return 42;
    }
};

} // namespace bozo

BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::sized_type, "sized_type", dynamic_size)

namespace {

using namespace testing;

using bozo::tests::sized_type;

TEST(data_frame_size, should_add_size_of_size_type_and_size_of_data) {
    EXPECT_EQ(bozo::data_frame_size(sized_type()), sizeof(bozo::size_type) + 42);
}

TEST(data_frame_size, for_empty_optional_should_be_equal_to_size_of_size_type) {
    EXPECT_EQ(bozo::data_frame_size(BOZO_STD_OPTIONAL<sized_type>()), sizeof(bozo::size_type));
}

} // namespace
