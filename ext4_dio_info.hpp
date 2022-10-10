#include "logger.hpp"
#include "ext4_dio_info.h"
#include "time_aq.hpp"
#include "hctx_state.hpp"
#include "heatmap.hpp"
extern "C" 
{
    #include "ext4_dio_info.skel.h"
}

class Ext4DioInfoModule;
static Ext4DioInfoModule* ext4_dio_info_module = NULL;
class Ext4DioInfoModule{
private:
    struct ext4_dio_info_bpf* skel;
    int err;
    struct ring_buffer* rb;
    unsigned long long processed = 0;
private:
    void bpf_load_and_verify(struct Env env);
    int __clean_up(){
        ext4_dio_info_bpf__destroy(skel);
        return 0;
    }
    int __init_logger_normal();
    static int proxy(void *ctx, void *data, size_t data_sz);
public:
    Logger* logger;

    //meta data process
    std::shared_ptr<TimeThroughputPmc> dio_throughtput;
    std::shared_ptr<Counter> file_op;
    std::shared_ptr<Counter> common_op;
    
    // info mode only
    //page, not use now
    //nr_phys_segments
    std::shared_ptr<HeatmapCollector> heatmap_trace;
    std::shared_ptr<HctxStateCollector> hctx_trace;
    std::shared_ptr<Hctx2disk> hctx_mapping;

    int do_event(const struct rq_info_event* log);
    int do_event(const struct dio_info_event* log);

    int do_meta(const struct dio_info_event* log);
    int do_meta(const struct rq_info_event* log);
    int do_meta(const struct hctx_metadata* state);
    //Set up ring buffer
    struct ring_buffer* set_up_ringbuf(struct ring_buffer *rb){
        if(rb == NULL){
            LOG(INFO) << "Ring_buffer need init";
            rb = ring_buffer__new(bpf_map__fd(skel->maps.ext4_dio_log),proxy, NULL, NULL);
            if(!rb)
                LOG(FATAL) << "Failed to make ringbuf";
            else
                LOG(INFO) << "Success make ringbuf";
        }else{
            LOG(INFO) << "Trying add " << logger->path << " to input ringbuf";
            err = ring_buffer__add(rb,bpf_map__fd(skel->maps.ext4_dio_log),proxy, NULL);
            if(!err)
                LOG(FATAL) << "Failed to add";
            else
                LOG(INFO) << "Success adding " << logger->path << " to input ringbuf";
        }
        return rb;
    }
    int release_logger(){
        //dump json file
        dio_throughtput->dump_json(logger->path);
        file_op->dump_json(logger->path);
        common_op->dump_json(logger->path);

        heatmap_trace->dump_json(logger->path);
        hctx_trace->dump_json(logger->path);
        hctx_mapping->dump_json(logger->path);
        delete logger;
        __clean_up();
        return 0;
    }
public:
    Ext4DioInfoModule(const struct Env env){
        //init logger
        LOG(INFO) << "Creating logger ..... ";
        logger = new Logger(env.trace_state.name,env.trace_state.depth_of_iouring);
        //register event log
        __init_logger_normal();
        logger->__register_to_io_uring();
        bpf_load_and_verify(env);
        err = ext4_dio_info_bpf__attach(skel);
        if (err) {
            LOG(INFO) << "Failed to attach BPF program " << logger->path;
            __clean_up();
        }
        LOG(INFO) << "Success attach " << logger->path;
        if(env.select_file){
            LOG(INFO) << "Start adding_target_file: ";
            bool has_inode = true;
            for(auto& id: env.inode){
                LOG(INFO) << "Trace_target_file_inode: " << id;
                int ret = bpf_map__update_elem(skel->maps.file_filter,&id,sizeof(unsigned long),&has_inode,sizeof(bool),BPF_NOEXIST);
                LOG_IF(FATAL,ret != 0) << "Fail to init file_trace";
            }
        }
    }
    ~Ext4DioInfoModule(){}
};

int Ext4DioInfoModule::proxy(void *ctx, void *data, size_t data_sz){
    switch(data_sz)
    {
        case sizeof(dio_info_event):{
            DLOG(INFO) << "Getting dio_info_event";
            ext4_dio_info_module->do_event(reinterpret_cast<struct dio_info_event*>(data));
            ext4_dio_info_module->do_meta(reinterpret_cast<struct dio_info_event*>(data));
            break;}
        case sizeof(hctx_metadata):{
            DLOG(INFO) << "Getting hctx_metadata";
            ext4_dio_info_module->do_meta(reinterpret_cast<struct hctx_metadata*>(data));
            break;}
        case sizeof(rq_info_event):{
            DLOG(INFO) << "Getting rq_info_event";
            ext4_dio_info_module->do_event(reinterpret_cast<struct rq_info_event*>(data));
            ext4_dio_info_module->do_meta(reinterpret_cast<struct rq_info_event*>(data));
            break;}
        default:
            LOG(FATAL) << "Getting unknown type message, exit";
    }
    return 0;
}

int Ext4DioInfoModule::do_event(const struct dio_info_event* log){
    processed++;
    unsigned long long* event_data = new unsigned long long[5];
    //dio_ptr
    event_data[0] = log->dio_uid;
    //end_dio
    event_data[1] = log->sim_event.end_block_io;
    //kernel_crossing
    event_data[2] = log->sim_event.start_ext4 - log->sim_event.start_dio;
    //fs
    event_data[3] = log->sim_event.start_block_io - log->sim_event.start_ext4;
    //block
    event_data[4] = log->sim_event.end_block_io - log->sim_event.start_block_io;
    std::string key = "dio_info_event";
    int index = logger->fd_map[key];
    int length = logger->file_format[index].row_size;
    logger->commit(key,reinterpret_cast<char*>(event_data),length);
    delete[] event_data;
    return 1;
}
int Ext4DioInfoModule::do_meta(const struct dio_info_event* log){
    dio_throughtput->commit(log->sim_event.end_block_io,log->sim_event.dio_size);
    const std::string dio_r_flag = "+dr";
    const std::string dio_w_flag = "+dw";
    std::string op_flag;
    switch (log->sim_event.type)
    {
    case D_READ:
        op_flag = dio_r_flag;
        break;
    case D_WRITE:
        op_flag = dio_w_flag;
        break;
    default:
        std::cout << log->sim_event.type << std::endl;
        LOG(FATAL) << "Unknown type of opeartion shoud be process";
        break;
    }
    auto file_name = std::string(log->sim_event.filename);
    file_op->commit(file_name+op_flag,1);
    auto process_str = std::string(log->sim_event.common);
    common_op->commit(process_str+op_flag,1);
    int select = 1;
    if(select){
        //log is selected, need record
        unsigned long long* meta_data = new unsigned long long[6];
        meta_data[0] = log->sim_event.type;
        meta_data[1] = log->sim_event.dio_size;
        __builtin_memcpy(&meta_data[2],log->sim_event.common,16);
        __builtin_memcpy(&meta_data[4],log->sim_event.filename,16);
        std::string key = "dio_info_event_meta";
        int index = logger->fd_map[key];
        int length = logger->file_format[index].row_size;
        logger->commit(key,reinterpret_cast<char*>(meta_data),length);
        delete[] meta_data;
    }
    return 0;
}

int Ext4DioInfoModule::do_event(const struct rq_info_event* log){
    // "dio_ptr",
    // "rq_ptr",
    // "block_sched",
    // "nvme_execute",
    // "scsi_execute",
    // "nvme_verify",
    // "scsi_verify",
    // "account_io"
    unsigned long long* event_data = new unsigned long long[8];
    event_data[0] = log->dio_uid;
    event_data[1] = log->rq_ptr;
    event_data[2] = log->queue_rq - log->rq_bio_start;
    if(log->rq_comp.drive == scsi_drive){
        DLOG(INFO) << "get the rq from scsi drive";
        event_data[3] = 0;
        event_data[4] = log->rq_comp.scsi_path.softirq_done - log->rq_comp.rq_start;
        event_data[5] = 0;
        event_data[6] = log->rq_comp.scsi_path.scsi_end-log->rq_comp.scsi_path.softirq_done;
        event_data[7] = log->rq_comp.account_io-log->rq_comp.scsi_path.scsi_end;
    }else if(log->rq_comp.drive == nvme_drive){
        DLOG(INFO) << "get the rq from nvme drive";
        event_data[3] = log->rq_comp.nvme_path.pci_complete_rq - log->rq_comp.rq_start;
        event_data[4] = 0;
        event_data[5] = log->rq_comp.nvme_path.nvme_complete_rq-log->rq_comp.nvme_path.pci_complete_rq;
        event_data[6] = 0;
        event_data[7] = log->rq_comp.account_io-log->rq_comp.nvme_path.nvme_complete_rq;
    }else{
        LOG(FATAL) << "Recv unknown type drive data";
    }
    std::string key = "rq_info_event";
    int index = logger->fd_map[key];
    int length = logger->file_format[index].row_size;
    logger->commit(key,reinterpret_cast<char*>(event_data),length);
    delete[] event_data;
    return 0;
}

int Ext4DioInfoModule::do_meta(const struct hctx_metadata* state){
    unsigned long long hctx_ptr = state->hctx_ptr;
    std::string drive(state->disk_name);
    auto ret = hctx_mapping->find(hctx_ptr);
    if(!ret.has_value()){
        std::string disk_name(state->disk_name);
        hctx_mapping->insert(hctx_ptr,disk_name);
        DLOG(INFO) << absl::StrFormat("First time get hctx %llu",hctx_ptr);
    }
    hctx_trace->commit(state);
    return 0;
}

int Ext4DioInfoModule::do_meta(const struct rq_info_event* log){
    heatmap_trace->commit(log,*hctx_mapping);
    return 0;
}


int Ext4DioInfoModule::__init_logger_normal(){
    struct file_with_format fwf_dio_info_event = {
        .file_name = std::string("dio_info_event"),
        .elems_size = std::vector<int>{8,8,8,8,8},
        .type_names = std::vector<std::string>{
            "dio_uid",
            "end_dio_io",
            "kernel_crossing",
            "file_system",
            "block_io"
        }
    };
    fwf_dio_info_event.row_size = accumulate(fwf_dio_info_event.elems_size.begin(),fwf_dio_info_event.elems_size.end(),0);
    logger->register_log(fwf_dio_info_event);
    struct file_with_format fwf_dio_info_event_meta = {
            .file_name = std::string("dio_info_event_meta"),
            .elems_size = std::vector<int>{8,8,16,16},
            .type_names = std::vector<std::string>{
                "type",
                "dio_size",
                "common",
                "filename"
            }
    };
    fwf_dio_info_event_meta.row_size = accumulate(fwf_dio_info_event_meta.elems_size.begin(),fwf_dio_info_event_meta.elems_size.end(),0);
    logger->register_log(fwf_dio_info_event_meta);

    struct file_with_format fwf_rq_info_event = {
        .file_name = std::string("rq_info_event"),
        .elems_size = std::vector<int>{8,8,8,8,8,8,8,8},
        .type_names = std::vector<std::string>{
            "dio_uid",
            "rq_ptr",
            "block_sched",
            "nvme_execute",
            "scsi_execute",
            "nvme_verify",
            "scsi_verify",
            "account_io"
        }
    };
    fwf_rq_info_event.row_size = accumulate(fwf_rq_info_event.elems_size.begin(),fwf_rq_info_event.elems_size.end(),0);
    logger->register_log(fwf_rq_info_event);
    return 0;
}

void Ext4DioInfoModule::bpf_load_and_verify(struct Env env)
{
    this->skel = ext4_dio_info_bpf__open();
    LOG_IF(FATAL,!skel) << absl::StrFormat("Failed to open BPF program: %s",strerror(errno));
    LOG(INFO) << "Loading ext4_dio_simple module ..... ";
    if(env.select_pid){
        skel->bss->using_pid_filter = 1;
        skel->bss->tpid_ext4_dio = env.target_pid;
        LOG(INFO) << "Trace_target_pid: " << env.target_pid;
    }
    
    if(env.select_common){
        skel->bss->using_common_filter = 1;
        assert(env.common_length <= 16);
        for(int i = 0;i < env.common_length;i++){
            skel->data->tcommon_name[i] = env.target_common[i];
        }
        LOG(INFO) << "Trace_target_common: " << std::string(env.target_common);
    }
     if(env.use_filter){
        LOG(FATAL) << "ext4_dio_info mode not support filter";
    }
    LOG(INFO) << "Creating meta_collecter :";
    
    LOG(INFO) << "dio_throughput";
    dio_throughtput = std::make_shared<TimeThroughputPmc>(env.pmc_inv,"dio_throughput");

    LOG(INFO) << "file operation counter";
    file_op = std::make_shared<Counter>("file_dio_op_counter");
    LOG(INFO) << "process operation counter";
    common_op = std::make_shared<Counter>("process_dio_op_counter");
    
    LOG(INFO) << "hctx_state tracer";
    hctx_trace = std::make_shared<HctxStateCollector>(env.pmc_inv);
    hctx_mapping = std::make_shared<Hctx2disk>();
    LOG(INFO) << "heatmap";
    heatmap_trace = std::make_shared<HeatmapCollector>(env.heatmap_row);
    

    LOG(INFO) << "Verify ext4_dio_simple module ..... ";
    err = ext4_dio_info_bpf__load(skel);
    if (err) {
        LOG(INFO) << "Failed to load and verify BPF skeleton";
        __clean_up();
    }
    LOG(INFO) << "Finish ext4_dio_simple load and verify";
}