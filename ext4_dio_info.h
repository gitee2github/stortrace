#pragma once
#ifndef _EXT4_DIO_INFO_H_
#define _EXT4_DIO_INFO_H_
#include "ext4_dio_simple.h"
struct dio_info_event{
    unsigned long long dio_uid;//use to join
    struct simple_dio_event sim_event;
};
#define nvme_drive 1
#define scsi_drive 2

struct hctx_metadata{
    unsigned long long hctx_ptr;
    time_stramp tp;
    unsigned long runed; 
    unsigned long queued;
    short int numa_node; 
    short int queue_num; 
    char disk_name[32];
}__attribute__((aligned(8)));

struct rq_info_event{
    unsigned long long dio_uid;//use to join
    unsigned long long rq_ptr;//hash key
    time_stramp rq_bio_start; //union key
    time_stramp queue_rq;
    struct rq_complete_event{
        time_stramp rq_start;
        union{
            struct {
                time_stramp softirq_done;
                time_stramp scsi_end;
            } scsi_path;
            struct {
                time_stramp pci_complete_rq;
                time_stramp nvme_complete_rq;
            } nvme_path;
        };
        time_stramp account_io;
        unsigned long long hctx_ptr;
        unsigned long long sector;
        unsigned short stats_sectors:16;
        unsigned short nr_phys_segments:16;
        unsigned int drive;
    }__attribute__((aligned(8)))rq_comp;
};
#endif