#ifndef _ENV_HPP_
#define _ENV_HPP_
#include "common.h"
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <optional>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>  
#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <numeric>
#include <memory>
#include <array>
#include <string.h>
#include <liburing.h>
#include <linux/io_uring.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <glog/logging.h>
#include "absl/strings/str_split.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_cat.h"
#include <absl/strings/numbers.h>
#include <absl/strings/str_join.h>
#include "absl/hash/hash.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/strip.h"
#include <cstdlib>
#include <iterator>
#include <limits>
#include "digestible.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <ctime>
using json = nlohmann::json;
static unsigned long get_inode(std::string& path);
static std::vector<unsigned long> path_to_inode(std::vector<std::string>& file_path);
enum filter_stage{
    SUM,
    //FOR EXT4_DIO_SIMPLE
    KERNEL_CROSSING,
    FILE_SYSTEM,
    BLOCK_IO
};
struct filter_config{
    int cold_start_iter;
    float quantile;
    int filter_accuracy;
    enum filter_stage stage;
};

struct event_statement{
    std::string name;
    int depth_of_iouring;
    enum event_type{DIO,FSYNC,BLK}e_type;
    enum trace_level{SIMPLE,INFO}t_level;
};
struct Env {
    struct event_statement trace_state;
    int common_length = 0;
    int target_pid;
    char target_common[16];
    std::vector<std::string> file_path;
    std::vector<unsigned long> inode;
    bool select_pid = false;
    bool select_common = false;
    bool select_file = true;
    bool use_filter = true;
    unsigned int pmc_inv;
    unsigned int heatmap_row;
    struct filter_config filter_conf;
    bool show_bpf_internal = true;
    absl::flat_hash_map<unsigned long,absl::string_view> id_to_file;
};

static unsigned long get_inode(std::string& path){
    int fd, inode;  
    fd = open(path.c_str(),O_RDONLY);
    if (fd < 0) {  
        LOG(FATAL) << absl::StrFormat("Error when getting file inode:%s",path);
    }  
    struct stat file_stat;  
    int ret = fstat(fd,&file_stat);  
    if (ret < 0) {  
        LOG(FATAL) << absl::StrFormat("Error when getting file inode:%s",path);
    } 
    inode = file_stat.st_ino;
    return inode;  
}

static std::vector<unsigned long> path_to_inode(std::vector<std::string>& file_path){
    std::vector<unsigned long> inode;
    int length = file_path.size();
    assert(length != 0);
    inode.resize(length);
    LOG(INFO) << "Getting target file info ......";
    for(int i = 0;i<length;i++){
        inode[i] = get_inode(file_path[i]);
        LOG(INFO) << absl::StrFormat("Adding %s with inode = %lu",file_path[i],inode[i]);
    }
    LOG(INFO) << absl::StrFormat("%d file selected",length);
    return inode;
}

static unsigned long long get_current_time(){
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    unsigned long long tp = nanoseconds.count();
    return tp;
}
static int read_config(std::string path,struct Env &env){
    LOG(INFO) << "Begin loading config...";
    std::ifstream f(path);
    json config = json::parse(f);
    
    env.trace_state.name = config["name"];
    LOG(INFO) << "Trace_task_name:" << env.trace_state.name;

    if(config["event_type"] == "dio"){
        env.trace_state.e_type = event_statement::DIO;
        LOG(INFO) << "Trace_event_type: direct_io";
    }else if(config["event_type"] == "blk"){
        env.trace_state.e_type = event_statement::BLK;
        LOG(INFO) << "Trace_event_type: blk";
    }
    else if(config["event_type"] == "buffered_io"){
        env.trace_state.e_type = event_statement::FSYNC;
        LOG(INFO) << "Trace_event_type: fsync";
    }else{
        LOG(FATAL) << "Unknown event_type select,please check config";
    }

    if(config["trace_level"] == "simple"){
        env.trace_state.t_level = event_statement::SIMPLE;
    }else if(config["trace_level"] == "info"){
        env.trace_state.t_level = event_statement::INFO;
    }else{
        LOG(FATAL) << "Unknown trace_level,please check config";
    }

    if(config["filter"]["enable"] == true){
        env.use_filter = true;
        LOG(INFO) << "enable latency filter";
        env.filter_conf.quantile = config["filter"]["quantile"];
        env.filter_conf.filter_accuracy = config["filter"]["filter_accuracy"];
        LOG(INFO) << absl::StrFormat("filter quantile: %f %%",env.filter_conf.quantile);
        LOG(INFO) << absl::StrFormat("filter_accuracy: %d",env.filter_conf.filter_accuracy);
        std::string select_stage = config["filter"]["stage"];
        if(select_stage == "sum"){
            env.filter_conf.stage = SUM;
            LOG(INFO) << "chose stage [sum]";
        }else if(select_stage == "kernel_crossing"){
            env.filter_conf.stage = KERNEL_CROSSING;
            LOG(INFO) << "chose stage [kernel_crossing]";
        }else if(select_stage == "file_system"){
            env.filter_conf.stage = FILE_SYSTEM;
            LOG(INFO) << "chose stage [file_system]";
        }else if(select_stage == "block_io"){
            env.filter_conf.stage = BLOCK_IO;
            LOG(INFO) << "chose stage [block_io]";
        }else{
            LOG(FATAL) << "Unknown filter stage,please check config";
        }
    }else{
        LOG(INFO) << "not using latency filter";
        env.use_filter = false;
    }
    env.trace_state.depth_of_iouring = config["logger_io_uring_depth"];
    LOG(INFO) << absl::StrFormat("logger_io_uring_depth %d",env.trace_state.depth_of_iouring);
    
    env.target_pid = config["select_target"]["pid"];
    if(env.target_pid == -1){
        env.select_pid = false;
        LOG(INFO) << "pid filter not use";
    }else{
        env.select_common = true;
        LOG(INFO) << absl::StrFormat("select_pid %d",env.target_pid);
    }

    std::string common_full = config["select_target"]["common"];
    LOG(INFO) << absl::StrFormat("try select_common: %s",common_full);
    env.common_length = 16;
    if(common_full == ""){
        env.select_common = false;
        LOG(INFO) << "common name filter not use";
    }else{
        env.select_common = true;
        if(common_full.length() > env.common_length){
            LOG(FATAL) << "Common_name filter only accept length 16, using prefix";
        }else{
            memset(&env.target_common,0,16);
            memcpy(&env.target_common,common_full.c_str(),common_full.length());
        }
        LOG(INFO) << absl::StrFormat("using select_common: %s",env.target_common);
    }
    env.file_path = config["select_target"]["files"].get<std::vector<std::string>>();
    if(env.file_path.size() == 0 || config["event_type"] == "blk"){
        env.select_file = false;
        LOG(INFO) << "file filter not use";
    }else{
        env.select_file = true;
        env.inode = path_to_inode(env.file_path);
        LOG(INFO) << "Trace_target: format : [file--inode]";
        for(int i = 0;i < env.inode.size();i++){
            LOG(INFO) << absl::StrFormat("[%s,%lu]",env.file_path[i],env.inode[i]);
            env.id_to_file.try_emplace(env.inode[i],env.file_path[i]);
        }
    }
    env.show_bpf_internal = config["show_bpf_internal"];
    LOG_IF(INFO,env.show_bpf_internal) << "show bpf internal = true";

    env.pmc_inv = config["statistical_params"]["pmc_inv"].get<unsigned long>()*1000000;
    LOG(INFO) << absl::StrFormat("Sampling frequency is %lu ms",env.pmc_inv / 1000000);

    env.heatmap_row = config["statistical_params"]["heatmap_row"].get<unsigned long>();
    LOG(INFO) << absl::StrFormat("Sampling heatmap_row is %lu",env.heatmap_row);
    return 0; 
}

#endif