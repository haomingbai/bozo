#include <bozo/failover/retry.h>

#include "../test_error.h"

#include <boost/hana/equal.hpp>
#include <boost/hana/not_equal.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

using namespace testing;
using time_point = bozo::time_traits::time_point;
using duration = bozo::time_traits::duration;
using namespace std::chrono_literals;

struct fake_connection_provider {};

struct get_try_time_constraint : Test {
    constexpr static auto deadline = time_point{} + 3s;
    constexpr static auto now() {return time_point{};}
};

struct connection_mock {
    MOCK_CONST_METHOD0(close_connection, void());
    friend void close_connection(connection_mock* self) {
        if(!self) {
            throw std::invalid_argument("self should not be null");
        }
        self->close_connection();
    };
};

} // namespace

// To make fake_connection_provider conforming to ConnectionProvider
// for test purposes
namespace bozo::detail {
template <typename T>
struct connection_provider_supports_time_constraint<fake_connection_provider, T>{
    using type = std::true_type;
};
template <typename T>
struct connection_provider_supports_time_constraint<fake_connection_provider*, T>{
    using type = std::true_type;
};
} // namespace bozo::detail

namespace bozo {
// Some cheats about connection_mock which is not a connection
// at all.
template <>
struct is_connection<connection_mock*> : std::true_type {};

template <>
struct is_nullable<connection_mock*> : std::true_type {};

} // namespace

namespace {

namespace hana = boost::hana;
using namespace std::literals;

TEST_F(get_try_time_constraint, should_return_none_for_none_time_constraint) {
    EXPECT_EQ(bozo::none,
        bozo::failover::detail::get_try_time_constraint(bozo::none, 1));
    EXPECT_EQ(bozo::none,
        bozo::failover::detail::get_try_time_constraint(bozo::none, -1));
    EXPECT_EQ(bozo::none,
        bozo::failover::detail::get_try_time_constraint(bozo::none, 0));
}

TEST_F(get_try_time_constraint, should_return_duration_divided_on_try_count_for_try_count_greter_than_zero) {
    EXPECT_EQ(1s, bozo::failover::detail::get_try_time_constraint(3s, 3));
}

TEST_F(get_try_time_constraint, should_return_zero_duration_for_try_count_zero) {
    EXPECT_EQ(duration{0}, bozo::failover::detail::get_try_time_constraint(3s, 0));
}

TEST_F(get_try_time_constraint, should_return_zero_duration_for_try_count_less_than_zero) {
    EXPECT_EQ(duration{0}, bozo::failover::detail::get_try_time_constraint(3s, -1));
}

TEST_F(get_try_time_constraint, should_return_time_left_divided_on_try_count_for_try_count_greter_than_zero) {
    EXPECT_EQ(1s, bozo::failover::detail::get_try_time_constraint(deadline, 3, now));
}

TEST_F(get_try_time_constraint, should_return_zero_time_left_for_try_count_zero) {
    EXPECT_EQ(duration{0}, bozo::failover::detail::get_try_time_constraint(deadline, 0, now));
}

TEST_F(get_try_time_constraint, should_return_zero_time_left_for_try_count_less_than_zero) {
    EXPECT_EQ(duration{0}, bozo::failover::detail::get_try_time_constraint(deadline, -1, now));
}

template <typename Errcs, typename Ctx>
static auto make_basic_try(int n_tries, Errcs errcs, Ctx ctx) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(op::tries = n_tries, op::conditions = errcs);
    return bozo::failover::basic_try(std::move(options), std::move(ctx));
}

struct basic_try__get_next_try : Test {
    connection_mock conn;
    struct handler_mock {
        MOCK_METHOD2(call, void(bozo::error_code, connection_mock*));
        void operator() (bozo::error_code ec, connection_mock* conn) { call(ec, conn); }
    };
    auto ctx() const {
        return bozo::failover::basic_context(fake_connection_provider{}, bozo::none);
    }
    static constexpr connection_mock* null_conn = nullptr;
    handler_mock handler;
};

TEST_F(basic_try__get_next_try, should_return_next_try_for_any_error_if_certain_is_not_specified) {
    const auto basic_try = [&] { return make_basic_try(3, hana::make_tuple(), ctx());};
    EXPECT_TRUE(basic_try().get_next_try(bozo::tests::error::error, null_conn));
    EXPECT_TRUE(basic_try().get_next_try(bozo::tests::error::another_error, null_conn));
    EXPECT_TRUE(basic_try().get_next_try(bozo::tests::error::ok, null_conn));
}

TEST_F(basic_try__get_next_try, should_return_next_try_for_matching_error_if_certain_is_specified) {
    auto basic_try = make_basic_try(3, hana::make_tuple(bozo::tests::errc::error), ctx());
    EXPECT_TRUE(basic_try.get_next_try(bozo::tests::error::another_error, null_conn));
}

TEST_F(basic_try__get_next_try, should_call_on_retry_handler_for_retry) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(
        op::tries = 3,
        op::on_retry = [&](bozo::error_code ec, auto& conn) mutable {handler(ec, conn);}
    );
    auto basic_try =  bozo::failover::basic_try(std::move(options), ctx());
    EXPECT_CALL(handler, call(bozo::error_code{bozo::tests::error::another_error}, null_conn));
    basic_try.get_next_try(bozo::tests::error::another_error, null_conn);
}

TEST_F(basic_try__get_next_try, should_not_call_on_retry_handler_if_no_retry_may_be) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(
        op::tries = 0,
        op::on_retry = [&](bozo::error_code ec, auto& conn) mutable {handler(ec, conn);}
    );
    auto basic_try = bozo::failover::basic_try(std::move(options), ctx());
    basic_try.get_next_try(bozo::tests::error::another_error, null_conn);
}

TEST_F(basic_try__get_next_try, should_return_null_state_for_nonmatching_error_if_certain_is_specified) {
    auto basic_try = make_basic_try(3, hana::make_tuple(bozo::tests::errc::error), ctx());
    EXPECT_FALSE(basic_try.get_next_try(bozo::tests::error::ok, null_conn));
}

TEST_F(basic_try__get_next_try, should_return_null_state_for_matching_error_and_no_tries_left) {
    auto first_try = make_basic_try(2, hana::make_tuple(), ctx());
    auto next_try = first_try.get_next_try(bozo::tests::error::error, null_conn);
    EXPECT_FALSE(next_try->get_next_try(bozo::tests::error::error, null_conn));
}

TEST_F(basic_try__get_next_try, should_close_connection_on_retry_if_option_is_omitted) {
    auto basic_try = make_basic_try(3, hana::make_tuple(), ctx());
    EXPECT_CALL(conn, close_connection());
    basic_try.get_next_try(bozo::tests::error::error, std::addressof(conn));
}

TEST_F(basic_try__get_next_try, should_close_connection_on_retry_if_option_is_true) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(op::tries=3, op::close_connection=true);
    auto basic_try =  bozo::failover::basic_try(std::move(options), ctx());
    EXPECT_CALL(conn, close_connection());
    basic_try.get_next_try(bozo::tests::error::error, std::addressof(conn));
}

TEST_F(basic_try__get_next_try, should_not_close_connection_on_retry_if_option_is_false) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(op::tries = 3, op::close_connection = false);
    auto basic_try =  bozo::failover::basic_try(std::move(options), ctx());
    basic_try.get_next_try(bozo::tests::error::error, std::addressof(conn));
}

TEST_F(basic_try__get_next_try, should_close_connection_on_no_retry_if_option_is_omitted) {
    auto basic_try = make_basic_try(3, hana::make_tuple(bozo::tests::errc::error), ctx());
    EXPECT_CALL(conn, close_connection());
    basic_try.get_next_try(bozo::tests::error::ok, std::addressof(conn));
}

TEST_F(basic_try__get_next_try, should_close_connection_on_no_retry_if_option_is_true) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(
        op::tries = 3,
        op::conditions = hana::make_tuple(bozo::tests::errc::error),
        op::close_connection = true
    );
    auto basic_try =  bozo::failover::basic_try(std::move(options), ctx());
    EXPECT_CALL(conn, close_connection());
    basic_try.get_next_try(bozo::tests::error::ok, std::addressof(conn));
}

TEST_F(basic_try__get_next_try, should_not_close_connection_on_no_retry_if_option_is_false) {
    using op = bozo::failover::retry_options;
    auto options = bozo::make_options(
        op::tries = 3,
        op::conditions = hana::make_tuple(bozo::tests::errc::error),
        op::close_connection = false
    );
    auto basic_try =  bozo::failover::basic_try(std::move(options), ctx());
    basic_try.get_next_try(bozo::tests::error::ok, std::addressof(conn));
}

struct basic_try__get_context : Test {
    fake_connection_provider provider;
    constexpr static auto errcs = hana::make_tuple();
};

TEST_F(basic_try__get_context, should_return_provider_form_context) {
    auto basic_try = make_basic_try(3, errcs,
        bozo::failover::basic_context(std::addressof(provider), bozo::none));
    EXPECT_EQ(basic_try.get_context()[hana::size_c<0>], std::addressof(provider));
}

TEST_F(basic_try__get_context, should_return_additional_arguments_form_context) {
    auto basic_try = make_basic_try(3, errcs,
        bozo::failover::basic_context(std::addressof(provider), bozo::none, 555, "strong"s));
    EXPECT_EQ(basic_try.get_context()[hana::size_c<2>], 555);
    EXPECT_EQ(basic_try.get_context()[hana::size_c<3>], "strong"s);
}

TEST_F(basic_try__get_context, should_return_claculated_time_out_form_context) {
    auto basic_try = make_basic_try(3, errcs,
        bozo::failover::basic_context(std::addressof(provider), 3s));
    EXPECT_EQ(basic_try.get_context()[hana::size_c<1>], 1s);
}

TEST(basic_try__tries_remain, should_return_tries_remain_count) {
    auto basic_try = make_basic_try(3, hana::make_tuple(),
        bozo::failover::basic_context(fake_connection_provider{}, bozo::none));
    EXPECT_EQ(basic_try.tries_remain(), 3);
}

template <typename Sequence>
static std::string to_string(const Sequence& v) {
    std::ostringstream s;
    boost::hana::fold(v, "("sv, [&s](std::string_view delim, const auto& item) {
            s << delim << item;
            return ", "sv;
        });
    s << ")";
    return s.str();
}

TEST(basic_try__retry_conditions, should_return_retry_conditions) {
    const auto conditions = hana::make_tuple(bozo::tests::errc::error, bozo::tests::error::ok);
    auto basic_try = make_basic_try(3, conditions,
        bozo::failover::basic_context(fake_connection_provider{}, bozo::none));
    EXPECT_EQ(basic_try.get_conditions(), conditions)
        << to_string(basic_try.get_conditions()) << "!=" << to_string(conditions);
}

} // namespace
