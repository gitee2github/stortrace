#pragma once
#ifndef _BLOCK_TRACE_H_
#define _BLOCK_TRACE_H_
#include "common.h"
enum IO_DIRECT{
    READ,
    WRITE
};
enum SCHEDU{
    UNKNOWN_SCHEDU,
    BYPASS,
    MQ_DEADLINE,
    BFQ,
    KYBER,
    NO_SCHEDU
};

enum DISPATCH{
    UNKNOWN_DISPATCH,
    DEQUEUE_CTX_,
    MQ_DEADLINE_,
    BFQ_,
    KYBER_,
    DIRECT_DISPATCH
};

enum DRIVE_TYPE{
    ERROR,
    NVME,
    SCSI
};

struct bio_enter {
    unsigned long long bio_ptr;
    unsigned long long bi_private;
    time_stramp tp;
}__attribute__((aligned(8)));

struct sched_insert{
    time_stramp sched;
    time_stramp dispatch;
};

struct direct_insert{
    time_stramp direct:64;
};

struct rq_trace_event{
    time_stramp bio_enter;
    char disk_name[32];
    char common_name[16];
    time_stramp rq_start;
    struct rq_submit_event{
        time_stramp plug;//plug-rq_start == plug
        union{
            struct sched_insert sched_path; //sched - plug == scheduling or 0
            struct direct_insert direct_path; //direct - plug
        };
        time_stramp queue_rq; //queue_rq - (direct/dispatch) == dispatch
        enum SCHEDU s_type;
        enum DISPATCH d_type;
        enum IO_DIRECT io_type;
    }__attribute__((aligned(8)))rq_sub;
    struct rq_complete_event{
        union{
            struct {
                time_stramp scsi_end;
            } scsi_path;
            struct {
                time_stramp nvme_complete_rq;
            } nvme_path;
        };
        time_stramp account_io;
        unsigned long long sector;
        unsigned short stats_sectors:16;
        unsigned short nr_phys_segments:16;
        enum DRIVE_TYPE drive;
    }__attribute__((aligned(8)))rq_comp;
};
#endif