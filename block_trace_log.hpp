#include "collector.hpp"
#ifndef _BLOCK_TRACE_LOG_HPP_
#define _BLOCK_TRACE_LOG_HPP_
struct bio_enter_record{
    unsigned long long bio_ptr;
    u64 tp;
};

struct blk_trace_record{
    u64 bio_enter;
    u64 alloc_request;
    u64 io_direction;
    u64 plug;
    u64 scheduling;
    u64 dispatch;
    u64 nvme_exec;
    u64 scsi_exec;
    u64 nvme_verify;
    u64 scsi_verify;
    char common[16];
};
#endif