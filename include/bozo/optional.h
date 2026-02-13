#pragma once

#if defined(__has_include)
#   if __has_include(<optional>)
#       define BOZO_HAS_STD_OPTIONAL 1
#   elif __has_include(<experimental/optional>)
#       define BOZO_HAS_STD_EXPIREMENTAL_OPTIONAL 1
#   endif
#elif
#   define BOZO_HAS_STD_OPTIONAL 1
#endif


#if defined(BOZO_HAS_STD_OPTIONAL)
#   define BOZO_STD_OPTIONAL std::optional
#   define BOZO_NULLOPT_T std::nullopt_t
#   define BOZO_NULLOPT std::nullopt
#   include <optional>
#elif defined(BOZO_HAS_STD_EXPIREMENTAL_OPTIONAL)
#   define BOZO_STD_OPTIONAL std::experimental::optional
#   define BOZO_NULLOPT_T std::experimental::nullopt_t
#   define BOZO_NULLOPT std::experimental::nullopt
#   include <experimental/optional>
#endif
