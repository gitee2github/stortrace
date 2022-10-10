#ifndef _HCTX_STATE_COLLECTOR_HPP_
#define _HCTX_STATE_COLLECTOR_HPP_
#include "Env.hpp"
#include "ext4_dio_info.h"
class HctxState{
private:
    //item: value,count-> for avg
    std::vector<std::pair<unsigned long long,unsigned long long>> pmc_data;
    char disk_name[32];
    short int numa_node; 
    //queue_num: Index of this hardware queue
    short int queue_num;
    
    unsigned long long hctx_ptr;
    time_stramp start_time;
    int pmc_size;
    unsigned long interval;
    bool started;
public:
    HctxState(int __interval,unsigned long long hctx)
    :interval(__interval),started(false),hctx_ptr(hctx){
        LOG(INFO) << absl::StrFormat("Create hctx %llu state collector",hctx_ptr);
        pmc_data.resize(30);
        pmc_size = 30;
    };
    int commit(const struct hctx_metadata* meta){
        if(!started){
            start_time = meta->tp;
            started = true;
            numa_node = meta->numa_node;
            __builtin_memcpy(disk_name,meta->disk_name,32);
            started = true;
            DLOG(INFO) << absl::StrFormat("Hctx drive: %s " , disk_name);
            DLOG(INFO) << absl::StrFormat("Started at: %llu " , start_time);
            DLOG(INFO) << absl::StrFormat("Numa_node: %d " , numa_node);
        }
        int index = (meta->tp-start_time) / interval;
        if(index>pmc_size){
            pmc_size *= 2;
            pmc_data.resize(pmc_size);
        }
        DLOG(INFO) << absl::StrFormat("runed: %lu ", meta->runed);
        DLOG(INFO) << absl::StrFormat("queued: %lu ", meta->queued);
        DLOG(INFO) << absl::StrFormat("index %d ", index);
        pmc_data[index].first = meta->runed;
        pmc_data[index].second = meta->queued;
        return 0;
    }
    int dump_json(std::string prefix){
        json result;
        //queue_num is index of this hardware queue,add the drive name to be unique key
        std::string name = absl::StrFormat("%d-%s",queue_num,disk_name);
        result["name"] = name;
        LOG(INFO) << absl::StrFormat("Creating hctx state info %s.json",name);
        result["hctx_ptr"] = absl::StrFormat("%llu",hctx_ptr);
        result["numa_node"] = numa_node;
        std::vector<std::string> str_data_runed;
        std::vector<std::string> str_data_queued;
        for(auto& item : pmc_data){
            str_data_runed.emplace_back(absl::StrFormat("%llu",item.first));
            str_data_queued.emplace_back(absl::StrFormat("%llu",item.second));
        }
        result["runed"] = absl::StrJoin(str_data_runed,",");
        result["queued"] = absl::StrJoin(str_data_queued,",");
        std::ofstream file(absl::StrFormat("%s/%s.json",prefix,name));
        file << result;
        file.close();
        LOG(INFO) << absl::StrFormat("Success dump %s.json",name);
        return 0;
    }
};

class HctxStateCollector{
public:
    absl::node_hash_map<unsigned long long,std::shared_ptr<HctxState>> collector;
    unsigned long interval;
public:
    HctxStateCollector(unsigned long acc):interval(acc){
        LOG(INFO) << "Create hctx_state_collector";
    };
    int commit(const struct hctx_metadata* meta){
        auto iter = collector.find(meta->hctx_ptr);
        if(iter == collector.end()){
            auto state = std::make_shared<HctxState>(interval,meta->hctx_ptr);
            collector.insert({meta->hctx_ptr,state});
        }else{
            iter->second->commit(meta);
        }
        return 0;
    }
    int dump_json(std::string prefix){
        for(auto& state:collector){
            state.second->dump_json(prefix);
        }
        return 0;
    }

};
#endif