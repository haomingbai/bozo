#pragma once

#include <bozo/pg/definitions.h>

namespace bozo::pg {
using oid = bozo::oid_t;
}

BOZO_PG_BIND_TYPE(bozo::pg::oid, "oid")
