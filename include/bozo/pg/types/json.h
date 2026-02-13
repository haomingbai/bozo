#pragma once

#include <bozo/pg/definitions.h>
#include <bozo/core/strong_typedef.h>

#include <string>

namespace bozo::pg {
BOZO_STRONG_TYPEDEF(std::string, json)
}

BOZO_PG_BIND_TYPE(bozo::pg::json, "json")
