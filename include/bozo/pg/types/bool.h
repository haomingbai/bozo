#pragma once

#include <bozo/pg/definitions.h>

namespace bozo::pg {
using bool_t = bool;
using boolean = bool_t;
}

BOZO_PG_BIND_TYPE(bozo::pg::bool_t, "bool")
