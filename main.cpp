#include <argp.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>  
#include <sys/stat.h> 
#include <gflags/gflags.h>
#include <thread>
#include "collector.hpp"

#include "ext4_dio_simple_log.h"
#include "ext4_dio_info_log.h"
#include "block_trace_log.hpp"

#include "ext4_dio_simple.hpp"
#include "ext4_dio_info.hpp"
#include "block_trace.hpp"

extern "C" 
{
    #include "common_um.h"
}
static struct Env env;
static void process_blk_trace_log(std::vector<std::pair<std::string,std::string>>& log_map, std::string &path,std::string &out_path){
    LogCollecterVector<blk_trace_record> blk_record(log_map[0].second,path);
    blk_record.make_table();
    LOG(INFO) << "Read blk_trace_record finish";
    LogCollecterVector<bio_enter_record> bio_enter(log_map[1].second,path);
    bio_enter.make_table();
    LOG(INFO) << "Read bio_enter_record finish";
    std::vector<std::vector<time_stramp>> latency(9,std::vector<time_stramp>(blk_record.table.size()));
    std::vector<std::string> common_name;
    std::vector<int> io_types;
    /*
    blk_end 
    time_stramp: when this bio end (start_time+latency)
    index: index to find the corresponding latency item
    */
    std::vector<std::pair<time_stramp,int>> blk_end(blk_record.table.size());
    for(int k = 0;k<blk_record.table.size();k++){
        common_name.emplace_back(std::string(blk_record.table[k].common));
        io_types.emplace_back(blk_record.table[k].io_direction);
        latency[0][k] = blk_record.table[k].alloc_request;
        latency[1][k] = blk_record.table[k].plug;
        latency[2][k] = blk_record.table[k].scheduling;
        latency[3][k] = blk_record.table[k].dispatch;
        latency[4][k] = blk_record.table[k].nvme_exec;
        latency[5][k] = blk_record.table[k].scsi_exec;
        latency[6][k] = blk_record.table[k].nvme_verify;
        latency[7][k] = blk_record.table[k].scsi_verify;
        for(int i = 0;i<8;i++){
            latency[8][k] += latency[i][k];
        }
        blk_end[k] = std::pair<time_stramp,int>(blk_record.table[k].bio_enter+latency[8][k],k);
    }
    std::sort(blk_end.begin(),blk_end.end(),[](const std::pair<time_stramp,int>& record1,const std::pair<time_stramp,int>& record2){return record1.first < record2.first;});
    LOG(INFO) << "Prepare latency data finish";
    std::vector<std::string> dis_list{
        "alloc_request",
        "plug",
        "scheduling",
        "dispatch",
        "nvme_exec",
        "scsi_exec",
        "nvme_verify",
        "scsi_verify",
        "sum"
    };
    json ioppmc = get_IO_pmc(env.pmc_inv,"sum",blk_end);
    dump_json(ioppmc,"sum_io_pmc","./"+out_path);
    LOG(INFO) << std::string_view("IO per PMC");
    for(int i = 0;i<dis_list.size();i++){
        json dis = get_dis(dis_list[i],latency[i]);
        dump_json(dis,dis_list[i]+"_dis","./"+out_path);
        LOG(INFO) << "Dump dis of " << dis_list[i];
        json avgs = get_avg_seq(env.pmc_inv,dis_list[i],latency[i],blk_end);
        dump_json(avgs,dis_list[i]+"_avg","./"+out_path);
        LOG(INFO) << "Dump avgs of " << dis_list[i];
    }

    json rw_rate = get_rw_rate(env.pmc_inv,"Read_write_percent",blk_end,io_types);
    dump_json(rw_rate,rw_rate["name"],"./"+out_path);
    LOG(INFO) << "RW_RATE finish";
    
    std::vector<time_stramp> before_merge;
    for(int i = 0;i<bio_enter.table.size();i++){
        before_merge.emplace_back(bio_enter.table[i].tp);
    }
    json merge_rate = get_merge_rate(env.pmc_inv,before_merge,blk_end);
    dump_json(merge_rate , "merge_rate" , "./" + out_path);


    LOG(INFO) << "MERGE_RATE finish";
    return;
}

static void process_dio_simple_event(std::vector<std::pair<std::string,std::string>>& log_map, std::string path,std::string out_path){
        LogCollecterVector<dio_simple_event_log> d_simple(log_map[0].second,path);
        d_simple.make_table();
        LogCollecterVector<dio_simple_meta_log> d_simple_meta(log_map[1].second,path);
        d_simple_meta.make_table();
        time_stramp start = d_simple.table[0].end_dio_io;

        std::vector<time_stramp> kernel_crossing_seq;
        std::vector<time_stramp> file_system_seq;
        std::vector<time_stramp> block_io_seq;

        std::vector<time_stramp> record_time;
        std::vector<time_stramp> sum_time;
        std::vector<std::string> commons;
        std::vector<std::string> files;
        std::vector<int> dio_index;
        
        int length = d_simple.table.size();

        for(int i = 0;i<length;i++){
            //latency data
            kernel_crossing_seq.emplace_back(d_simple.table[i].kernel_crossing);
            file_system_seq.emplace_back(d_simple.table[i].file_system);
            block_io_seq.emplace_back(d_simple.table[i].block_io);
            //extra data
            record_time.emplace_back(d_simple.table[i].end_dio_io-start);
            commons.emplace_back(std::string(d_simple_meta.table[i].common));
            files.emplace_back(std::string(d_simple_meta.table[i].filename));
            //index
            dio_index.emplace_back(i);
        }
        sum_time.resize(length);
        for(int i = 0;i<length;i++){
            sum_time[i] = kernel_crossing_seq[i]+file_system_seq[i]+block_io_seq[i];
        }
        //here no select just record, go write json
        json stramp_seq;
        stramp_seq["name"] = "dio_record_time";
        stramp_seq["x_data"] = record_time;
        stramp_seq["y_data"] = sum_time;
        stramp_seq["dio_index"] = dio_index;

        json latency_of_dio_info;

        latency_of_dio_info["name"] = "dio_event_latency";
        latency_of_dio_info["sum"] = sum_time;
        latency_of_dio_info["common"] = commons;
        latency_of_dio_info["file"] = files;
        latency_of_dio_info["kernel_crossing"] = kernel_crossing_seq;
        latency_of_dio_info["file_system"] = file_system_seq;
        latency_of_dio_info["block_io"] = block_io_seq;

        dump_json(stramp_seq,stramp_seq["name"],"./"+out_path);
        dump_json(latency_of_dio_info,latency_of_dio_info["name"],"./"+out_path);
        return;
}

static void process_dio_info_event(std::vector<std::pair<std::string,std::string>>& log_map,std::string path,std::string out_path){
        LogCollecterVector<dio_info_event_log> d_info(log_map[0].second,path);
        d_info.make_table();
        LogCollecterVector<dio_info_event_meta_log> d_info_meta(log_map[1].second,path);
        d_info_meta.make_table();
        LogCollecterVector<rq_info_event_log> rq_info(log_map[2].second,path);
        rq_info.make_table();
        time_stramp start = d_info.table[0].end_dio_io;
        std::vector<time_stramp> kernel_crossing_seq;
        std::vector<time_stramp> file_system_seq;
        std::vector<time_stramp> block_io_seq;

        std::vector<time_stramp> record_time;
        std::vector<time_stramp> sum_time;
        std::vector<std::string> commons;
        std::vector<std::string> files;
        std::vector<int> dio_index;
        
        int length = d_info.table.size();
        
        for(int i = 0;i<length;i++){
            //latency data
            kernel_crossing_seq.emplace_back(d_info.table[i].kernel_crossing);
            file_system_seq.emplace_back(d_info.table[i].file_system);
            block_io_seq.emplace_back(d_info.table[i].block_io);
            //extra data
            record_time.emplace_back(d_info.table[i].end_dio_io-start);
            commons.emplace_back(std::string(d_info_meta.table[i].common));
            files.emplace_back(std::string(d_info_meta.table[i].filename));
            //index
            dio_index.emplace_back(i);
        }
        sum_time.resize(length);
        for(int i = 0;i<length;i++){
            sum_time[i] = kernel_crossing_seq[i]+file_system_seq[i]+block_io_seq[i];
        }
        //here no select just record, go write json
        json stramp_seq;
        stramp_seq["name"] = "dio_record_time";
        stramp_seq["x_data"] = record_time;
        stramp_seq["y_data"] = sum_time;
        stramp_seq["dio_index"] = dio_index;

        json latency_of_dio_info;

        latency_of_dio_info["name"] = "dio_event_latency";
        latency_of_dio_info["sum"] = sum_time;
        latency_of_dio_info["common"] = commons;
        latency_of_dio_info["file"] = files;
        latency_of_dio_info["kernel_crossing"] = kernel_crossing_seq;
        latency_of_dio_info["file_system"] = file_system_seq;
        latency_of_dio_info["block_io"] = block_io_seq;

        dump_json(stramp_seq,stramp_seq["name"],"./"+out_path);
        dump_json(latency_of_dio_info,latency_of_dio_info["name"],"./"+out_path);

        json dio_rq;
        std::string dir_name = out_path+"/rq_result";
        struct stat st = {0};
        if(stat(dir_name.c_str(), &st) == -1) {
            mkdir(dir_name.c_str(), 0777);
        } else {
            LOG(FATAL) << "please using clean data, rq_result existed";
        }
        auto join_dio_ptr = hashjoin_vector(0,0,d_info.table,rq_info.table,1);
        absl::node_hash_map<int,std::vector<int>> dio_rq_map;
        for(auto& item:join_dio_ptr){
            DLOG(INFO) << absl::StrFormat("[dio %d- rq %d]",item.first,item.second);
            auto iter = dio_rq_map.find(item.first);
            if(iter == dio_rq_map.end()){
                dio_rq_map.try_emplace(item.first,std::vector<int>());
                iter = dio_rq_map.find(item.first);
            }
            iter->second.emplace_back(item.second);
        }
        for(auto& each_dio:dio_rq_map){
            DLOG(INFO) << "process dio index " << each_dio.first;
            //using enter time as unique key
            json this_dio_rq_data;
            this_dio_rq_data["dio_index"] = std::to_string(each_dio.first);
            for(auto& row: each_dio.second){
                DLOG(INFO) << "write rq index " << row;
                std::vector<time_stramp> stage;
                stage.resize(6);
                stage[0] = rq_info.table[row].block_sched;
                stage[1] = rq_info.table[row].nvme_execute;
                stage[2] = rq_info.table[row].nvme_verify;
                stage[3] = rq_info.table[row].scsi_execute;
                stage[4] = rq_info.table[row].scsi_verify;
                stage[5] = rq_info.table[row].account_io;
                this_dio_rq_data[std::to_string(row)]["latency"] = stage;
                DLOG(INFO) << "write rq index " << row << " latency finish";

            }
            dump_json(this_dio_rq_data,this_dio_rq_data["dio_index"],"./"+out_path+dir_name);
        }
        return;
}
DEFINE_string(mode,"trace","define the mode of stortrace task");
DEFINE_string(conf,"","path to the trace mode config, json format");
DEFINE_string(log_path,"","path to log record data, set at config of trace mode");
DEFINE_string(extra_path,"","path to dump visualization data");
DEFINE_string(vis_data,"","path of visualization data");
DEFINE_string(port,"10010","port of the visualization server run");
DEFINE_string(ip,"127.0.0.1","ip of the visualization server run");
DEFINE_string(flask_server,"../vis/server.py","visualization server path");
DECLARE_bool(help);
DECLARE_string(helpmatch);
int main(int argc, char* argv[])
{
    gflags::SetVersionString("1.0.0");
    gflags::SetUsageMessage("\n"
    "Disk_io trace and analysis tools for openeuler based on ebpf\n"
    "Document: \nhttps://gitee.com/openeuler/stortrace"
    );
    gflags::ParseCommandLineNonHelpFlags(&argc, &argv, true);
    if(FLAGS_help){
        FLAGS_help = false;
        FLAGS_helpmatch = "main";
    }
    gflags::HandleCommandLineHelpFlags();
    google::InitGoogleLogging(argv[0]);
    FLAGS_stderrthreshold = google::INFO;  
    read_config(FLAGS_conf,env);
    show_bpf_internal = env.show_bpf_internal;
    if(FLAGS_mode == "trace"){
        LOG_IF(INFO,setup()) << "setup success";
        struct ring_buffer *rb = NULL;
        if(env.trace_state.e_type == event_statement::DIO){
            if(env.trace_state.t_level == event_statement::SIMPLE){
                ext4_dio_sim_module = new Ext4DioSimpleModule(env);
                rb = ext4_dio_sim_module->set_up_ringbuf(rb);
            }else if(env.trace_state.t_level == event_statement::INFO){
                ext4_dio_info_module = new Ext4DioInfoModule(env);
                rb = ext4_dio_info_module->set_up_ringbuf(rb);
            }else{
                LOG(FATAL) << "unsupport level";
            }
        }else if(env.trace_state.e_type == event_statement::BLK){
            blk_trace_module = new BlkTraceModule(env);
            rb = blk_trace_module->set_up_ringbuf(rb);
        }else{
                LOG(FATAL) << "unsupport event";
        }
        while (!exiting) {
            ring_buffer__poll(rb, 100 /* timeout, ms */);
        }
        if(ext4_dio_sim_module){
            ext4_dio_sim_module->release_logger();
        }
        if(ext4_dio_info_module){
            ext4_dio_info_module->release_logger();
        }
        if(blk_trace_module){
            blk_trace_module->release_logger();
        }
        LOG(INFO) << "Successfully Finish Tracing!";
        std::string path = env.trace_state.name;
        LOG(INFO) << env.trace_state.name;
        std::string out_path = env.trace_state.name+"_dump";
        struct stat st = {0};
        system(absl::StrFormat("rm -rf %s",out_path).c_str());
        if(stat(out_path.c_str(), &st) == -1){
            mkdir(out_path.c_str(), 0777);
            LOG(INFO) << "Success make out_dir folder";
            system(absl::StrFormat("cp %s/*.json ./%s",path,out_path).c_str());
        }
        LOG(INFO) << "Begin loading log_meta...";
        std::ifstream log_meta(path + "/log_meta");
        LOG(INFO) << path + "/log_meta";
        std::vector<std::pair<std::string,std::string>> log_map;
        for(std::string line; std::getline(log_meta,line);)
        {
            DLOG(INFO) << line;
            std::vector<std::string> log_splite = absl::StrSplit(line," ");
            log_map.emplace_back(std::pair<std::string,std::string>(log_splite[0],line));
        }
        if(log_map[0].first == "blk_trace_record"){
            process_blk_trace_log(log_map,path,out_path);    
        }else if(log_map[0].first == "ext4_dio_simple_event"){
            process_dio_simple_event(log_map,path,out_path);
        }else if(log_map[0].first == "dio_info_event"){
            process_dio_info_event(log_map,path,out_path);
        }
    }
    else if(FLAGS_mode == "display"){
        std::string out_path = env.trace_state.name + "_dump";
        system(absl::StrFormat("python3 %s %s %s %s",FLAGS_flask_server,out_path,FLAGS_ip,FLAGS_port).c_str());   
    }
    else{
        LOG(FATAL) << "target not exist"; 
    }
    google::ShutdownGoogleLogging();
    return 0;
}
