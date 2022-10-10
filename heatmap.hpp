#ifndef _HEATMAP_HPP_
#define _HEATMAP_HPP_
#include "Env.hpp"
#include "ext4_dio_info.h"
#include "block_trace.h"

class Heatmap{
private:
    std::string drive_name;
    unsigned long long max;
    unsigned long long min;
    std::vector<std::pair<unsigned long long,unsigned short>> recv_data;
    int partition;
    std::vector<int> bucket;
public:
    Heatmap(int par,std::string name):drive_name(name),partition(par*8){
        max = 0;
        min = INT64_MAX;
        LOG(INFO) << absl::StrFormat("Creating Heatmap for %s with partition %d",drive_name,partition);
    };
    int commit(unsigned long long _sector,unsigned short stats);
    int gen_heatmap(){
        LOG(INFO) << absl::StrFormat("Range of collect sector cursor of %s is:[%llu,%llu]",drive_name,min,max);
        //make partition
        unsigned long long range = max-min;
        int size = range / (partition) + 100;//plus as bias
        bucket.resize(range / size + 1);
        for(auto& item: recv_data){
            bucket[(item.first - min) / size] += item.second;
        }
        LOG(INFO) << "Success to make heatmap";
        return 0;
    };
    int dump_json(std::string prefix){
        json result;
        DLOG(INFO) << absl::StrFormat("dump heatmap for %s",drive_name);
        result["name"] = drive_name;
        result["partition"] = partition;
        std::vector<std::string> str_data;
        for(auto& value:bucket){
            DLOG(INFO) << "heatmap_v:" << value;
            str_data.emplace_back(std::to_string(value));
        }
        result["seq_data"] = absl::StrJoin(str_data,",");
        LOG(INFO) << "Making json data success";
        std::ofstream file(absl::StrFormat("%s/%s_heatmap.json",prefix,drive_name));
        file << result;
        file.close();
        LOG(INFO) << absl::StrFormat("Success dump %s_heatmap.json",drive_name);
        return 0;
    };
};
int Heatmap::commit(unsigned long long _sector,unsigned short stats){
    if(_sector > max){
        max = _sector;
    }
    if(_sector < min){
        min = _sector;
    }
    recv_data.emplace_back(std::pair<unsigned long long,unsigned short>(_sector,stats));
    DLOG(INFO) << absl::StrFormat("commit value :%llu,%d",_sector,stats);
    return 0;
}

class Hctx2disk{
public:
    absl::flat_hash_map<unsigned long long,std::string> hctx_map;
    Hctx2disk(){};
public:
    std::optional<std::string> find(unsigned long long hctx){
        auto iter = hctx_map.find(hctx);
        if(iter != hctx_map.end())
            return iter->second;
        else{
            DLOG(INFO) << "No hctx recorded";
            return std::nullopt;
        }
    };
    int insert(unsigned long long hctx,std::string drive){
        hctx_map.insert({hctx,drive});
        return 0;
    }
    int dump_json(std::string prefix){
        json result;
        result["name"] = "hctx_to_drive";
        std::vector<std::string> str_data;
        for(auto& item:hctx_map){
            std::string item_str = std::to_string(item.first)+" "+item.second;
            str_data.emplace_back(item_str);
        }
        result["seq_data"] = absl::StrJoin(str_data,",");
        LOG(INFO) << "Making json data success";
        std::ofstream file(absl::StrFormat("%s/%s.json",prefix,"hctx_to_drive"));
        file << result;
        file.close();
        LOG(INFO) << absl::StrFormat("Success dump %s.json","hctx_to_drive");
        return 0;
    };

};

class HeatmapCollector{
public:
    int par;
    absl::flat_hash_map<std::string,std::shared_ptr<Heatmap>> collector;
    HeatmapCollector(int acc):par(acc){
        LOG(INFO) << absl::StrFormat("Hosting the heatmap collector with %d * 8",acc);
    };
public:
    int commit(const rq_trace_event* rq_trace);
    int commit(const struct rq_info_event* rq_info,Hctx2disk&);
    int dump_json(std::string prefix);
};

int HeatmapCollector::commit(const rq_trace_event* rq_trace){
    std::string disk_name(rq_trace->disk_name);
    auto iter = collector.find(disk_name);
    if(iter == collector.end()){
        //this disk have not register yet
        std::shared_ptr<Heatmap> heatmap_ptr = std::make_shared<Heatmap>(par,disk_name);
        collector.insert({disk_name,heatmap_ptr});
        //now can insert
        iter = collector.find(disk_name);
        DLOG(INFO) << absl::StrFormat("make heatmap for %s",disk_name);
    }
    DLOG(INFO) << absl::StrFormat("commit to %s",disk_name);
    iter->second->commit(rq_trace->rq_comp.sector,rq_trace->rq_comp.stats_sectors);
    return 0;
};

int HeatmapCollector::commit(const struct rq_info_event* rq_info,Hctx2disk& hctx_map){
    auto ret = hctx_map.find(rq_info->rq_comp.hctx_ptr);
    std::string disk_name;
    if(ret.has_value()){
        disk_name = ret.value();
    }else{
        LOG(FATAL) << "rec unknow disk";
    }
    auto iter = collector.find(disk_name);
    if(iter == collector.end()){
        //this disk have not register yet
        std::shared_ptr<Heatmap> heatmap_ptr = std::make_shared<Heatmap>(par,disk_name);
        collector.insert({disk_name,heatmap_ptr});
        //now can insert
        iter = collector.find(disk_name);
        DLOG(INFO) << absl::StrFormat("make heatmap for %s" , disk_name);
    }
    DLOG(INFO) << absl::StrFormat("commit to %s",disk_name);
    iter->second->commit(rq_info->rq_comp.sector,rq_info->rq_comp.stats_sectors);
    return 0;
};
int HeatmapCollector::dump_json(std::string prefix){
    for(auto& map:collector){
        map.second->gen_heatmap();
        map.second->dump_json(prefix);
    }
    return 0;
};

#endif