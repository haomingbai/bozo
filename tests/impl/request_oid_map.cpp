#include "test_asio.h"

#include <bozo/connection_info.h>
#include <bozo/impl/request_oid_map.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace bozo::tests {

struct custom_type1 {};
struct custom_type2 {};

} // namespace bozo::tests

BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::custom_type1, "custom_type1")
BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::custom_type2, "custom_type2")

namespace {

using namespace testing;
using namespace bozo::tests;

TEST(get_types_names, should_return_empty_container_for_empty_oid_map) {
    auto type_names = bozo::impl::get_types_names(bozo::empty_oid_map{});
    EXPECT_TRUE(type_names.empty());
}

TEST(get_types_names, should_return_type_names_from_oid_map) {
    auto type_names = bozo::impl::get_types_names(
        bozo::register_types<custom_type1, custom_type2>());
    EXPECT_THAT(type_names, ElementsAre("custom_type1", "custom_type2"));
}

TEST(set_oid_map, should_set_oids_for_oid_map_from_oids_result_argument) {
    auto oid_map = bozo::register_types<custom_type1, custom_type2>();
    const bozo::impl::oids_result res = {11, 22};
    bozo::impl::set_oid_map(oid_map, res);
    bozo::impl::get_types_names(bozo::empty_oid_map{});
    EXPECT_EQ(bozo::type_oid<custom_type1>(oid_map), 11u);
    EXPECT_EQ(bozo::type_oid<custom_type2>(oid_map), 22u);
}

TEST(set_oid_map, should_throw_on_oid_map_size_is_not_equal_to_oids_result_size) {
    auto oid_map = bozo::register_types<custom_type1, custom_type2>();
    const bozo::impl::oids_result res = {11};
    EXPECT_THROW(bozo::impl::set_oid_map(oid_map, res), std::length_error);
}

TEST(set_oid_map, should_throw_on_null_oid_in_oids_result) {
    auto oid_map = bozo::register_types<custom_type1, custom_type2>();
    const bozo::impl::oids_result res = {11, bozo::null_oid};
    EXPECT_THROW(bozo::impl::set_oid_map(oid_map, res), std::invalid_argument);
}

template <class OidMap = bozo::empty_oid_map>
struct connection {
    OidMap oid_map_;
    std::string error_context_;
    using error_context = std::string;

    const error_context& get_error_context() const noexcept { return error_context_; }
    void set_error_context(error_context v = error_context{}) { error_context_ = std::move(v); }

    OidMap& oid_map() noexcept {
        return oid_map_;
    }
    const OidMap& oid_map() const noexcept {
        return oid_map_;
    }
};

}

namespace bozo {
template <class OidMap>
struct is_connection<::connection<OidMap>> : std::true_type {};
} // namespace bozo

namespace {
TEST(request_oid_map_op, should_call_handler_with_oid_request_failed_error_when_oid_map_length_differs_from_result_length) {
    StrictMock<callback_gmock<connection<>>> cb_mock {};
    auto operation = bozo::impl::request_oid_map_op{wrap(cb_mock)};
    operation.ctx_->res_ = bozo::impl::oids_result(1);

    EXPECT_CALL(cb_mock, call(bozo::error_code(bozo::error::oid_request_failed), _)).WillOnce(Return());
    operation(bozo::error_code {}, connection {});
}

} // namespace
