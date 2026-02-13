#pragma once

#include <libpq-fe.h>

namespace bozo::impl {

inline const char* get_result_status_name(ExecStatusType status) noexcept {
#define BOZO_CASE_RETURN(item) case item: return #item;
    switch (status) {
        BOZO_CASE_RETURN(PGRES_SINGLE_TUPLE)
        BOZO_CASE_RETURN(PGRES_TUPLES_OK)
        BOZO_CASE_RETURN(PGRES_COMMAND_OK)
        BOZO_CASE_RETURN(PGRES_COPY_OUT)
        BOZO_CASE_RETURN(PGRES_COPY_IN)
        BOZO_CASE_RETURN(PGRES_COPY_BOTH)
        BOZO_CASE_RETURN(PGRES_NONFATAL_ERROR)
        BOZO_CASE_RETURN(PGRES_BAD_RESPONSE)
        BOZO_CASE_RETURN(PGRES_EMPTY_QUERY)
        BOZO_CASE_RETURN(PGRES_FATAL_ERROR)
#ifdef LIBPQ_HAS_PIPELINING
        BOZO_CASE_RETURN(PGRES_PIPELINE_SYNC)
        BOZO_CASE_RETURN(PGRES_PIPELINE_ABORTED)
#endif
#ifdef LIBPQ_HAS_CHUNK_MODE
        BOZO_CASE_RETURN(PGRES_TUPLES_CHUNK)
#endif
    }
#undef BOZO_CASE_RETURN
    return "unknown";
}

} // namespace bozo::impl
