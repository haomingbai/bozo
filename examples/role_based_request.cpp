#include <bozo/connection_info.h>
#include <bozo/request.h>
#include <bozo/shortcuts.h>
#include <bozo/failover/role_based.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>

#include <iostream>

namespace asio = boost::asio;
namespace hana = boost::hana;
namespace failover = bozo::failover;

const auto print_error = [](bozo::error_code ec, const auto& conn) {
    std::cout << "error code message: \"" << ec.message();
    // Here we should check if the connection is in null state to avoid UB.
    if (!bozo::is_null_recursive(conn)) {
        std::cout << "\", libpq error message: \"" << bozo::error_message(conn)
            << "\", error context: \"" << bozo::get_error_context(conn);
    }
    std::cout << "\"";
};

const auto print_fallback = [](bozo::error_code ec, const auto& conn, const auto& fallback) {
    print_error(ec, conn);
    // We can print information about fallback will be used for next try
    if constexpr (decltype(fallback.role() == bozo::failover::master)::value) {
        std::cout << " fallback is \"master\"" << std::endl;
    } else {
        std::cout << " fallback is \"replica\"" << std::endl;
    }
};

int main(int argc, char **argv) {
    std::cout << "BOZO role-based request failover example" << std::endl;

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <master connection string> <replica connection string>\n";
        return 1;
    }

    asio::io_context io;

    // Here we provide a mapping of roles to connection strings
    auto conn_info = failover::make_role_based_connection_source(
        failover::master=bozo::connection_info(argv[1]),
        failover::replica=bozo::connection_info(argv[2])
    );

    asio::spawn(io, [&] (asio::yield_context yield) {
        using namespace bozo::literals;
        using namespace std::chrono_literals;
        using opt = failover::role_based_options;

        bozo::rows_of<int> result;
        bozo::error_code ec;
        // Here we will try operation on master first and then replica if any problem will take place
        // Each try will have its own time constraint
        //   master try will be limited by 1/2 sec.
        //   replica try will be limited by  (1 - t(1st try)) / 2 sec, which is not less than 1/2 sec.
        auto roles = failover::role_based(failover::master, failover::replica)
        // We want to print out information about retries
                        .set(opt::on_fallback = print_fallback);
        // Here a request call with role based failover strategy
        auto conn = bozo::request[roles](conn_info[io], "SELECT 1"_SQL, 1s, bozo::into(result), yield[ec]);

        // When request is completed we check is there an error.
        if (ec) {
            std::cout << "Request failed; ";
            print_error(ec, conn);
            std::cout << std::endl;
            return;
        }

        // Just print request result
        std::cout << "Selected:" << std::endl;
        for (auto value : result) {
            std::cout << std::get<0>(value) << std::endl;
        }
    });

    io.run();

    return 0;
}
