#pragma once

#include <bozo/pg/definitions.h>
#include <string>

/**
 * @defgroup group-ext-std-string std::string
 * @ingroup group-ext-std
 * @brief [std::string](https://en.cppreference.com/w/cpp/string/basic_string)
 *
 *@code
#include <bozo/ext/std/string.h>
 *@endcode
 *
 * `std::string` is mapped as `text` PostgreSQL type.
 */

BOZO_PG_BIND_TYPE(std::string, "text")

/**
 * @defgroup group-ext-std-string_view std::string_view
 * @ingroup group-ext-std
 * @brief [std::string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)
 *
 *@code
#include <bozo/ext/std/string.h>
 *@endcode
 *
 * `std::string_view` is mapped as `text` PostgreSQL type.
 * @note It can be used as a query parameter only!
 */

BOZO_PG_BIND_TYPE(std::string_view, "text")
