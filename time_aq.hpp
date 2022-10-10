#ifndef _TIME_AQ_HPP_
#define _TIME_AQ_HPP_
#include "Env.hpp"
class TimeAvgPmc{
private:
    std::vector<std::pair<unsigned long long,int>> pmc_data;
    std::string name;
    time_stramp start_time;
    unsigned long interval;
    int pmc_size;
    bool started;
public:
    TimeAvgPmc(int __interval,std::string key)
    :interval(__interval),name(key),started(false){
        LOG(INFO) << absl::StrFormat("Create %s with interval %lu ns",name,interval);
        pmc_data.resize(30);
        pmc_size = 30;
    };
    int commit(time_stramp tp,unsigned long long data){
        if(!started){
            LOG(INFO) << absl::StrFormat("Started %s at %llu",name,tp);
            start_time = tp;
            started = true;
        }
        u64 passed = tp - start_time;
        int index = -1;
        index = passed / interval;
        DLOG_IF(FATAL,index < 0) << "order error";
        if(index > pmc_size - 1){
            DLOG(INFO) << absl::StrFormat("passed %d",passed);
            DLOG(INFO) << absl::StrFormat("Commit index %d",index);
            DLOG(INFO) << absl::StrFormat("pmc size of %s is %d, expand to %d",name,pmc_size,pmc_size*2);
            pmc_size *= 2;
            pmc_data.resize(pmc_size);
        }
        pmc_data[index].first += data;
        pmc_data[index].second++;
        return 0;
    }
    json get_json(){
        json result;
        result["name"] = name;
        std::vector<u64> avgs;
        for(auto& aq:pmc_data){
            if(aq.second){
                avgs.emplace_back(aq.first / aq.second);
                DLOG(INFO) << absl::StrFormat("avg : [%llu,%llu,%llu]",aq.first,aq.second,aq.first / aq.second);
            }else{
                avgs.emplace_back(0);
            }
        }
        result["avgs"] = avgs;
        return result;
    };
};


class TimeThroughputPmc{
public:
    std::vector<unsigned long long> pmc_data;
private:
    std::string name;
    time_stramp start_time;
    unsigned long interval;
    int pmc_size;
    bool started;
public:
    TimeThroughputPmc(int __interval,std::string key)
    :interval(__interval),name(key),started(false){
        LOG(INFO) << absl::StrFormat("Create %s with interval %lu ns",name,interval);
        pmc_data.resize(30);
        pmc_size = 30;
    };
    int commit(time_stramp tp,unsigned long long data){
        if(!started){
            LOG(INFO) << absl::StrFormat("Started %s at %llu",name,tp);
            start_time = tp;
            started = true;
        }
        u64 passed = tp - start_time;
        int index = -1;
        index = passed / interval;
        DLOG_IF(FATAL,index < 0) << "order error";
        if(index > pmc_size - 1){
            DLOG(INFO) << absl::StrFormat("pmc size of %s is %d, expand to %d",name,pmc_size,pmc_size*2);
            pmc_size *= 2;
            pmc_data.resize(pmc_size);
        }
        pmc_data[index] += data;
        return 0;
    }

    json get_json(){
        json result;
        result["name"] = name;
        result["seq_data"] = pmc_data;
        return result;
    }
    int dump_json(std::string prefix){
        json result;
        result["name"] = name;
        std::vector<std::string> str_data;
        for(auto& aq:pmc_data){
            str_data.emplace_back(std::to_string(aq));
        }
        result["seq_data"] = absl::StrJoin(str_data,",");
        LOG(INFO) << "Making json data success";
        std::ofstream file(absl::StrFormat("%s/%s.json",prefix,name));
        file << result;
        file.close();
        LOG(INFO) << absl::StrFormat("Success dump %s.json",name);
        return 0;
    };
};

class Counter{
private:
    std::string __name;
    absl::flat_hash_map<std::string,unsigned long> count_map;
public:
    Counter(std::string name):__name(name){};
    int commit(absl::string_view key,int add_count);
    int dump_json(std::string prefix);
};
int Counter::commit(absl::string_view key,int add_count){
    auto iter = count_map.find(key);
    //has key
    if(iter != count_map.end()){
        iter->second += add_count;
    }else{
        count_map.insert({key.data(),1});
    }
    return 0;
}
int Counter::dump_json(std::string prefix){
    json result;
        result["name"] = __name;
        std::vector<std::string> str_data;
        for(auto& item:count_map){
            str_data.emplace_back(absl::StrFormat("[%s,%lu]",item.first,item.second));
        }
        result["seq_data"] = absl::StrJoin(str_data,",");
        LOG(INFO) << "Making json data success";
        std::ofstream file(absl::StrFormat("%s/%s.json",prefix,__name));
        file << result;
        file.close();
        LOG(INFO) << absl::StrFormat("Success dump %s.json",__name);
    return 0;
}
#endif