#pragma once
#include "collector.hpp"
#ifndef _EXT4_DIO_SIMPLE_LOG_HPP_
#define _EXT4_DIO_SIMPLE_LOG_HPP_
struct dio_simple_event_log{
   u64 end_dio_io;         
   u64 kernel_crossing;         
   u64 file_system;
   u64 block_io;
};
struct dio_simple_meta_log{
   u64 type;
   u64 dio_size;
   char common[16];
   char filename[16];
};
#endif