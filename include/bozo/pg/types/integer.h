#pragma once

#include <bozo/pg/definitions.h>

namespace bozo::pg {

using int2 = int16_t;
using smallint = int2;
using int4 = int32_t;
using integer = int4;
using int8 = int64_t;
using bigint = int8;

} // namespace bozo::pg

BOZO_PG_BIND_TYPE(bozo::pg::int8, "int8")
BOZO_PG_BIND_TYPE(bozo::pg::int4, "int4")
BOZO_PG_BIND_TYPE(bozo::pg::int2, "int2")
