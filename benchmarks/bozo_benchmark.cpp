#include "benchmark.h"

#include <bozo/connection_info.h>
#include <bozo/query_builder.h>
#include <bozo/request.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>

#include <iostream>

int main(int argc, char *argv[]) {
    using namespace bozo::literals;
    using namespace bozo::benchmark;

    namespace asio = boost::asio;

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <conninfo>" << std::endl;
        return 1;
    }

    rows_count_limit_benchmark benchmark(10000000);
    asio::io_context io(1);
    bozo::connection_info connection_info(argv[1]);
    const auto query = ("SELECT typname, typnamespace, typowner, typlen, typbyval, typcategory, "_SQL +
                        "typispreferred, typisdefined, typdelim, typrelid, typelem, typarray "_SQL +
                        "FROM pg_type WHERE typtypmod = "_SQL +
                        -1 + " AND typisdefined = "_SQL + true).build();

    for (int i = 0; i < 8; ++i) {
        asio::spawn(io, [&] (auto yield) {
            try {
                auto connection = bozo::get_connection(connection_info[io], yield);
                benchmark.start();
                while (true) {
                    bozo::result result;
                    bozo::request(connection, query, std::ref(result), yield);
                    if (!benchmark.step(result.size())) {
                        break;
                    }
                }
            } catch (const std::exception& e) {
                std::cout << e.what() << '\n';
            }
        });
    }

    io.run();

    return 0;
}
