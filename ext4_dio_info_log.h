#pragma once
#ifndef _EXT4_DIO_INFO_LOG_HPP_
#define _EXT4_DIO_INFO_LOG_HPP_
#include "collector.hpp"
struct dio_info_event_log{
    u64 dio_ptr;
    u64 end_dio_io;
    u64 kernel_crossing;
    u64 file_system;
    u64 block_io;
};

struct dio_info_event_meta_log{
    u64 type;
    u64 dio_size;
    char common[16];
    char filename[16];
};

struct rq_info_event_log{
    u64 dio_ptr;
    u64 rq_ptr;
    u64 block_sched;
    u64 nvme_execute;
    u64 scsi_execute;
    u64 nvme_verify;
    u64 scsi_verify;
    u64 account_io;
};
#endif