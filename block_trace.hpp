#include "logger.hpp"
#include "block_trace.h"
#include "time_aq.hpp"
#include "heatmap.hpp"
extern "C" 
{
    #include "block_trace.skel.h"
}

class BlkTraceModule;
static BlkTraceModule* blk_trace_module = NULL;
class BlkTraceModule{
private:
    struct block_trace_bpf* skel;
    int err;
    struct ring_buffer* rb;
    unsigned long long processed = 0;
private:
    void bpf_load_and_verify(struct Env env);
    int __clean_up(){
        block_trace_bpf__destroy(skel);
        return 0;
    }
    int __init_logger_normal();
    static int proxy(void *ctx, void *data, size_t data_sz);
public:
    Logger* logger;
    std::shared_ptr<Counter> common_op;
    std::shared_ptr<HeatmapCollector> heatmap_trace;

    //for bio enter
    int do_event(const struct bio_enter* log);
    //for request finish
    int do_event(const struct rq_trace_event* log);
    //for heatmap
    int do_meta(const struct rq_trace_event* log);

    //Set up ring buffer
    struct ring_buffer* set_up_ringbuf(struct ring_buffer *rb){
        if(rb == NULL){
            LOG(INFO) << "Ring_buffer need init";
            rb = ring_buffer__new(bpf_map__fd(skel->maps.blk_log),proxy, NULL, NULL);
            if(!rb)
                LOG(FATAL) << "Failed to make ringbuf";
            else
                LOG(INFO) << "Success make ringbuf";
        }else{
            LOG(INFO) << "Trying add " << logger->path << " to input ringbuf";
            err = ring_buffer__add(rb,bpf_map__fd(skel->maps.blk_log),proxy, NULL);
            if(!err)
                LOG(FATAL) << "Failed to add";
            else
                LOG(INFO) << "Success adding " << logger->path << " to input ringbuf";
        }
        return rb;
    }
    int release_logger(){
        //dump json file
        common_op->dump_json(logger->path);
        heatmap_trace->dump_json(logger->path);
        delete logger;
        __clean_up();
        return 0;
    }
public:
    BlkTraceModule(const struct Env env){
        //init logger
        LOG(INFO) << "Creating logger ..... ";
        logger = new Logger(env.trace_state.name,env.trace_state.depth_of_iouring);
        //register event log
        __init_logger_normal();
        logger->__register_to_io_uring();
        bpf_load_and_verify(env);
        err = block_trace_bpf__attach(skel);
        if (err) {
            LOG(INFO) << "Failed to attach BPF program " << logger->path;
            __clean_up();
        }
        LOG(INFO) << "Success attach " << logger->path;
    }
    ~BlkTraceModule(){}
};

int BlkTraceModule::proxy(void *ctx, void *data, size_t data_sz){
    switch(data_sz)
    {
        case sizeof(rq_trace_event):{
            DLOG(INFO) << "Getting rq_trace_event";
            auto log = reinterpret_cast<struct rq_trace_event*>(data);
            if(log->rq_sub.plug){
                blk_trace_module->do_event(reinterpret_cast<struct rq_trace_event*>(data));
                blk_trace_module->do_meta(reinterpret_cast<struct rq_trace_event*>(data));
            }
            break;}
        case sizeof(bio_enter):{
            DLOG(INFO) << "Getting bio_enter";
            blk_trace_module->do_event(reinterpret_cast<struct bio_enter*>(data));
            break;}
        default:
            LOG(FATAL) << "Getting unknown type message, exit";
    }
    return 0;
}

int BlkTraceModule::do_event(const struct rq_trace_event* log){
    processed++;
    unsigned long long* event_data = new unsigned long long[12];
    //bio_enter_time 
    event_data[0] = log->bio_enter;
    //alloc_request |enter bio_mq_submit ---- getting req|
    event_data[1] = log->rq_start - log->bio_enter;
    DLOG_IF(FATAL,event_data[1] < 0) << "Error when compute alloc_request";
    //crossing |getting req ---- add to plug|
    if(log->rq_sub.plug>0){   
        // event_data[2] = log->rq_sub.plug - log->rq_start;
        DLOG_IF(FATAL,event_data[2] < 0) << "Error when compute crossing";
    }else{
        DLOG(FATAL) << "NO PLUG, drop";
        delete[] event_data;
        return 0;
    }
    if(log->rq_sub.io_type == WRITE){
        event_data[2] = WRITE;
    }else{
        event_data[2] = READ;
    }
    //plug |add to plug ---- directly issue/insert|
    //scheduling |insert ---- dispatch|
    if(log->rq_sub.s_type == NO_SCHEDU){
        //no sched
        event_data[3] = log->rq_sub.direct_path.direct - log->rq_sub.plug;
        DLOG_IF(FATAL,log->rq_sub.direct_path.direct<log->rq_sub.plug) << "Error for plug";
        DLOG_IF(FATAL,event_data[3] < 0) << "Error when compute plug";
        event_data[4] = 0;
    }else if(log->rq_sub.s_type != UNKNOWN_SCHEDU){
        //have sched
        event_data[3] = log->rq_sub.sched_path.sched - log->rq_sub.plug;
        DLOG_IF(FATAL,event_data[3] < 0) << "Error when compute plug";
        event_data[4] = log->rq_sub.sched_path.dispatch - log->rq_sub.sched_path.sched;
        DLOG_IF(FATAL,event_data[4] < 0) << "Error when compute sched";
    }else{
        DLOG(FATAL) << "UNKNOWN_SCHEDU";
    }
    //dispatch
    if(log->rq_sub.d_type == DIRECT_DISPATCH){
        //nvme path
        event_data[5] = log->rq_sub.queue_rq - log->rq_sub.direct_path.direct;
    }else if(log->rq_sub.d_type == UNKNOWN_DISPATCH){
        DLOG(INFO) << "direct: " << log->rq_sub.direct_path.direct;
        DLOG(INFO) << "plug: " << log->rq_sub.plug;
        DLOG(INFO) << "DISPATCH: " << log->rq_sub.d_type;
        //DLOG(FATAL)<<"UNKNOWN_DISPATCH PATH";
        //bug here
        delete[] event_data;
        return 0;
    }else{
        //scsi
        event_data[5] = log->rq_sub.queue_rq - log->rq_sub.sched_path.dispatch;
    }
    DLOG_IF(FATAL,event_data[5] < 0) << "Error when compute dispatch";

    //exec
    if(log->rq_comp.drive == NVME){
        //nvme
        event_data[6] = log->rq_comp.nvme_path.nvme_complete_rq - log->rq_sub.queue_rq;
        event_data[7] = 0;
        DLOG_IF(FATAL,event_data[6] < 0) << "Error when compute nvme exec";
    }else if(log->rq_comp.drive == SCSI){
        //scsi
        event_data[6] = 0;
        event_data[7] = log->rq_comp.scsi_path.scsi_end - log->rq_sub.queue_rq;
        DLOG_IF(FATAL,event_data[7] < 0) << "Error when compute scsi exec";
    }else{
        DLOG(FATAL) << "Unknow drive type";
    }

    //vefify
    if(log->rq_comp.drive == NVME){
        event_data[8] = log->rq_comp.account_io - log->rq_comp.nvme_path.nvme_complete_rq;
        event_data[9] = 0;
    }else if(log->rq_comp.drive == SCSI){
        event_data[8] = 0;
        event_data[9] = log->rq_comp.account_io - log->rq_comp.scsi_path.scsi_end;
    }else{
        DLOG(FATAL) << "Unknow drive type";
    }
    __builtin_memcpy(&event_data[10],log->common_name,16);
    std::string key = "blk_trace_record";
    int index = logger->fd_map[key];
    int length = logger->file_format[index].row_size;
    logger->commit(key,reinterpret_cast<char*>(event_data),length);
    delete[] event_data;
    return 1;
}
int BlkTraceModule::do_event(const struct bio_enter* log){
    unsigned long long* event_data = new unsigned long long[2];
    event_data[0] = log->bio_ptr;
    event_data[1] = log->tp;
    std::string key = "bio_enter_record";
    int index = logger->fd_map[key];
    int length = logger->file_format[index].row_size;
    logger->commit(key,reinterpret_cast<char*>(event_data),length);
    delete[] event_data;
    return 0;
}

int BlkTraceModule::do_meta(const struct rq_trace_event* log){
    if(log->rq_sub.plug){
        heatmap_trace->commit(log);
    }
    return 0;
}
int BlkTraceModule::__init_logger_normal(){
    struct file_with_format fwf_trace = {
        .file_name = std::string("blk_trace_record"),
        .elems_size = std::vector<int>{8,8,8,8,8,8,8,8,8,8,16},
        .type_names = std::vector<std::string>{
            "bio_enter",
            "alloc_request",
            "io_direction",
            "plug",
            "scheduling",
            "dispatch",
            "nvme_exec",
            "scsi_exec",
            "nvme_verify",
            "scsi_verify",
            "common_name"
        }
    };
    fwf_trace.row_size = accumulate(fwf_trace.elems_size.begin(),fwf_trace.elems_size.end(),0);
    logger->register_log(fwf_trace);
    struct file_with_format fwf_bio_enter = {
            .file_name = std::string("bio_enter_record"),
            .elems_size = std::vector<int>{8,8},
            .type_names = std::vector<std::string>{
                "bio_ptr",
                "enter_time",
            }
    };
    fwf_bio_enter.row_size = accumulate(fwf_bio_enter.elems_size.begin(),fwf_bio_enter.elems_size.end(),0);
    logger->register_log(fwf_bio_enter);
    return 0;
}

void BlkTraceModule::bpf_load_and_verify(struct Env env)
{
    this->skel = block_trace_bpf__open();
    LOG_IF(FATAL,!skel) << absl::StrFormat("Failed to open BPF program: %s",strerror(errno));
    LOG(INFO) << "Loading blk_trace module ..... ";
    if(env.select_pid){
        skel->bss->using_pid_filter = 1;
        skel->bss->tpid_block_trace = env.target_pid;
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
        LOG(FATAL) << "blk_trace mode not support filter";
    }
    LOG(INFO) << "Creating meta_collecter :";
    LOG(INFO) << "process operation counter";
    common_op = std::make_shared<Counter>("process_counter");
    LOG(INFO) << "heatmap";
    heatmap_trace = std::make_shared<HeatmapCollector>(env.heatmap_row);
    

    LOG(INFO) << "Verify blk_trace module ..... ";
    err = block_trace_bpf__load(skel);
    if (err) {
        LOG(INFO) << "Failed to load and verify BPF skeleton";
        __clean_up();
    }
    LOG(INFO) << "Finish blk_trace load and verify";
}