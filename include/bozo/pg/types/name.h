#pragma once

#include <bozo/pg/definitions.h>
#include <bozo/core/strong_typedef.h>

#include <string>

namespace bozo::pg {
BOZO_STRONG_TYPEDEF(std::string, name)
}

BOZO_PG_BIND_TYPE(bozo::pg::name, "name")
