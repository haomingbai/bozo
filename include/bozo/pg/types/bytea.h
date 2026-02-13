#pragma once

#include <bozo/pg/definitions.h>
#include <bozo/core/strong_typedef.h>

#include <vector>

namespace bozo::pg {
BOZO_STRONG_TYPEDEF(std::vector<char>, bytea)
}

BOZO_PG_BIND_TYPE(bozo::pg::bytea, "bytea")
