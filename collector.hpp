#ifndef _COMMON_HPP_
#define _COMMON_HPP_
typedef long long unsigned int u64;
#include <iostream>
#include <string>
#include <vector>
#include <glog/logging.h>
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/numbers.h"
#include "absl/hash/hash.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include <cstdlib>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <absl/container/node_hash_map.h>
#include "absl/strings/strip.h"
#include <memory>
#include <iterator>
#include <limits>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include "time_aq.hpp"

typedef unsigned long long time_stramp;
using json = nlohmann::json;

template <class T>
static void print64(T *s){
    int n = sizeof(*s);
    u64* temp = reinterpret_cast<u64*>(s);
    for(int i = 0;i < n / 8;i++){
        std::cout << absl::StrFormat("item %d: %llu",i,temp[i]) << std::endl;
    }
}

template <class T>
static inline u64 itemk(T *s,int k){
    u64* temp = reinterpret_cast<u64*>(s);
    return temp[k];
}

template <class T>
class LogCollecterVector{
public:
    std::string log_name;
    std::vector<std::pair<std::string,unsigned int>> data_format;
    std::vector<T> table;
    size_t n;
    int row_size = 0;
    int fd;
    LogCollecterVector(std::string line,std::string prefix){
        std::vector<std::string> format = absl::StrSplit(line,' ');
        log_name = format[0];
        LOG(INFO) << absl::StrFormat("collecter name: %s",log_name);
        absl::string_view s(format[1]);
        s = absl::StripSuffix(s, "|");
        auto types = absl::StrSplit(s,'|');
        for(auto& type : types){
            std::vector<std::string> item = absl::StrSplit(type,"--"); 
            int size;
            if(absl::SimpleAtoi(item[1],&size)){
                row_size += size;
                data_format.emplace_back(std::pair<std::string,int>(item[0],size));
                DLOG(INFO) << absl::StrFormat("Data format: %s -- %d ",item[0],size);
            }
        }
        if(absl::SimpleAtoi(format[2],&n)){
            DLOG(INFO) << "PATH " << prefix + "/" + log_name;
            fd = open((prefix + "/" + log_name).c_str(),O_RDONLY);
            DLOG(INFO) << absl::StrFormat("fd = %d, row_size = %d, row_counts = %d", fd,row_size,n);
        }
    }
    int make_table(){
        if(n == 0){
            LOG(INFO) << absl::string_view("No elements");
            return 0;
        }
        table.resize(n);
        DLOG(INFO) << absl::StrFormat("row_size: %d",row_size);
        DLOG(INFO) << absl::StrFormat("sizeof(T): %d",sizeof(T));
        assert(sizeof(T) == row_size);
        read(fd,table.data(),row_size*n);
        LOG(INFO) << "table has: " << table.size() << " items";
        return 0;
    }
    ~LogCollecterVector(){
        LOG(INFO) << "close log " << log_name;
    };
};

template<class V1,class V2>
std::vector<std::pair<int,int>> hashjoin_vector(int i,int j,std::vector<V1> &table_A,std::vector<V2> &table_B,int mode){
    std::vector<std::pair<int,int>> join_result;
    std::multimap<u64,u64> join_table;  
    for(auto &ele : table_A){
        //map to hash as {V_n,K_1,K_2......}
        int index_A = &ele - &table_A[0];
        u64 select = itemk<V1>(&ele,i);
        //std::cout<<absl::StrFormat("select table_A value = %llu,index = %llu",select,index_A)<<std::endl;
        join_table.insert({select,index_A});
    }
    for(auto &ele : table_B){
        u64 select = itemk<V2>(&ele,j);
        auto iter = join_table.find(select);
        //per_to_per join
        if(mode == 0){
            if(iter != join_table.end()){
            int index_B  = &ele - &table_B[0];
            int index_A = iter->second;
            
            // std::cout<<absl::StrFormat("joined A_index = %llu,B_index = %llu",index_A,index_B)<<std::endl;
            u64 v1 = itemk<V1>(&table_A[index_A],i);
            u64 v2 = itemk<V2>(&table_B[index_B],j);
            assert( v1 == v2 );

            join_result.emplace_back(std::pair<int,int>(index_A,index_B));
            join_table.erase(iter);
            }
        } 
        //m to n join
        else if(mode == 1){
            int m = join_table.count(select);
            for(int n = 0;n<m;n++,iter++){
                int index_B = &ele - &table_B[0];
                int index_A = iter->second;
                // std::cout<<absl::StrFormat("joined A_index = %llu,B_index = %llu",index_A,index_B)<<std::endl;
                u64 v1 = itemk<V1>(&table_A[index_A],i);
                u64 v2 = itemk<V2>(&table_B[index_B],j);
                assert(v1 == v2);
                join_result.emplace_back(std::pair<int,int>(index_A,index_B));
            }
        }else{
            DLOG(FATAL) << "unkown mode";
        } 
    }
    return join_result;
}

static int dump_json(json &result,std::string name,std::string prefix){
    LOG(INFO) << "Path to dump:" << absl::StrFormat("%s/%s.json",prefix,name);
    std::ofstream file(absl::StrFormat("%s/%s.json",prefix,name));
    file << result;
    file.close();
    LOG(INFO) << absl::StrFormat("Success dump %s.json",name);
    return 0;
}

static json get_dis(std::string dis_name,std::vector<u64>& latency){
    u64 min_v = INT64_MAX;
    u64 max_v = 0;
    for(auto& v:latency){
        if(v > max_v) max_v = v;
        if(v < min_v) min_v = v;
    }
    DLOG(INFO) << "Max Min " << max_v << " " << min_v;
    u64 step = (max_v - min_v) / 100;
    if(step == 0){
        step = 1;
    }
    DLOG(INFO) << "Bucket step " << step;
    std::vector<int> bucket(101);
    for(int i = 0;i<latency.size();i++){
        u64 v = latency[i];
        int index = (v - min_v) / step;
        if(index >= 100){
            index = 100;
        }
        bucket[index]++;
    }
    json distribution;
    distribution["name"] = dis_name;
    distribution["max_value"] = max_v;
    distribution["min_value"] = min_v;
    distribution["distribution"] = bucket;
    return distribution;
}

static json get_avg_seq(u64 pmc,std::string name,std::vector<u64>& latency,std::vector<std::pair<time_stramp,int>>& time_seq){
    TimeAvgPmc avg_pmc(pmc,name);
    for(int i = 0;i<latency.size();i++){
        DLOG(INFO) << absl::StrFormat("time: %llu, latency: %llu" , time_seq[i].first , latency[time_seq[i].second]);
        avg_pmc.commit(time_seq[i].first,latency[time_seq[i].second]);
    }
    json avgs = avg_pmc.get_json();
    return avgs;
}

static json get_IO_pmc(u64 pmc,std::string name,std::vector<std::pair<time_stramp,int>>& time_seq){
    TimeThroughputPmc IO_pmc(pmc,name);
    for(int i = 0;i<time_seq.size();i++){
        IO_pmc.commit(time_seq[i].first,1);
    }
    json IOPMCS = IO_pmc.get_json();
    return IOPMCS;
}

// static json get_common_dim(u64 pmc,std::string name,){

// }


static json get_rw_rate(u64 pmc,std::string name,std::vector<std::pair<time_stramp,int>>& time_seq,std::vector<int>& io_types){
    /*
    @param:
    pmc : time piece
    name : name_of_plot_title
    time_seq : io_end_time_stramp  [bio_end_time,bio_enter_time_index]
    @return 
    json : include meta data, and for each time piece, compute Read/Write ratio
    */
    TimeThroughputPmc Read_pmc(pmc,"");
    TimeThroughputPmc Write_pmc(pmc,"");
    //commit the first time_stramp to align
    Read_pmc.commit(time_seq[0].first,1);
    Write_pmc.commit(time_seq[0].first,1);
    for(auto& t: time_seq){
        int index = t.second;
        switch (io_types[index])
        {
        case 0:
            Read_pmc.commit(t.first,1);
            break;
        case 1:
            Write_pmc.commit(t.first,1);
            break;
        default:
            DLOG(FATAL) << "Catching unknown type IO";
            break;
        }
    }
    auto Read_seq = Read_pmc.pmc_data;
    auto Write_seq = Write_pmc.pmc_data;
    int less_size = Read_seq.size() < Write_seq.size() ? Read_seq.size() : Write_seq.size();
    std::vector<float> Read_rate;
    std::vector<float> Write_rate;
    for(int i = 0;i<less_size;i++){
        float R_count = static_cast<float>(Read_seq[i]);
        float W_count = static_cast<float>(Write_seq[i]);
        DLOG(INFO) << "R_count: " << R_count;
        DLOG(INFO) << "W_count: " << W_count;
        float All_count = R_count + W_count;
        if(R_count > 1 && W_count > 1){
            Read_rate.emplace_back(W_count / All_count);
            Write_rate.emplace_back(R_count / All_count);
        }else{
            Read_rate.emplace_back(0);
            Write_rate.emplace_back(0);
        }
    }
    json rw_rate;
    rw_rate["r_rate"] = Read_rate;
    rw_rate["w_rate"] = Write_rate;
    rw_rate["name"] = name;
    return rw_rate;
}


static json get_merge_rate(u64 pmc,std::vector<u64>& before,std::vector<std::pair<time_stramp,int>>& after)
{
    json merge_rate;
    std::vector<float> rate;
    TimeThroughputPmc IO_before(pmc,"");
    TimeThroughputPmc IO_after(pmc,"");
    for(auto& t: before){
        IO_before.commit(t,1);
    }
    IO_after.commit(before[0],1);
    for(auto& t: after){
        IO_after.commit(t.first,1);
    }
    for(int i = 0;i < IO_after.pmc_data.size();i++){
        float all = static_cast<float>(IO_before.pmc_data[i]);
        float merged = static_cast<float>(IO_after.pmc_data[i]);
        DLOG(INFO) << "all: " << all;
        DLOG(INFO) << "merged: " << merged;
        if(all > 1){
            rate.emplace_back((all - merged) / all);
            DLOG(INFO) << "rate: " << (all - merged) / all;
        }else{
            rate.emplace_back(0);
        }
    }
    merge_rate["rate"] = rate;
    return merge_rate;
}
#endif