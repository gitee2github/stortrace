#pragma once
#ifndef _EXT4_DIO_SIMPLE_H_
#define _EXT4_DIO_SIMPLE_H_
#include "common.h"
enum dio_type{
    D_WRITE,
    D_READ
};
struct simple_dio_event{
    time_stramp start_dio;
    time_stramp start_ext4;
    time_stramp start_block_io;
    time_stramp end_block_io;
    long long dio_size;
    unsigned long inode_id;
    char common[16];
    char filename[16];
    enum dio_type type;
};
#endif