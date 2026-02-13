#pragma once

#include <bozo/pg/definitions.h>

namespace bozo::pg {
using char_t = char;
}

BOZO_PG_BIND_TYPE(bozo::pg::char_t, "char")
