#pragma once

#include <bozo/pg/definitions.h>

namespace bozo::pg {

using float8 = double;
using double_precision = float8;
using float4 = float;
using real = float4;

} // namespace bozo::pg

BOZO_PG_BIND_TYPE(bozo::pg::float8, "float8")
BOZO_PG_BIND_TYPE(bozo::pg::float4, "float4")
