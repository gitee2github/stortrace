#include "logger.hpp"
#include "ext4_dio_simple.h"
#include "time_aq.hpp"
extern "C" 
{
    #include "ext4_dio_simple.skel.h"
}

class Ext4DioSimpleModule;
static Ext4DioSimpleModule* ext4_dio_sim_module = NULL;
class Ext4DioSimpleModule{
private:
    struct ext4_dio_simple_bpf* skel;
    int err;
    struct ring_buffer* rb;
    unsigned long long processed = 0;
    bool using_filter = false;
private:
    void bpf_load_and_verify(struct Env env);
    int __clean_up(){
        ext4_dio_simple_bpf__destroy(skel);
        return 0;
    }
    int __init_logger_normal();
    static int proxy(void *ctx, void *data, size_t data_sz);
public:
    Logger* logger;
    digestible::Tdigest<unsigned int,unsigned int>* filter;
    
    std::shared_ptr<TimeThroughputPmc> dio_throughtput;
    std::shared_ptr<Counter> file_op;
    std::shared_ptr<Counter> common_op;
    
    filter_config filter_conf;
    int do_event(const struct simple_dio_event* log);
    int do_meta(const struct simple_dio_event* log,int select);
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
        delete logger;
        __clean_up();
        return 0;
    }
public:
    Ext4DioSimpleModule(const struct Env env){
        //init logger
        LOG(INFO) << "Creating logger ..... ";
        logger = new Logger(env.trace_state.name,env.trace_state.depth_of_iouring);
        //register event log and 
        __init_logger_normal();
        logger->__register_to_io_uring();
        bpf_load_and_verify(env);
        err = ext4_dio_simple_bpf__attach(skel);
        if (err) {
            LOG(INFO) << "Failed to attach BPF program " << logger->path;
            __clean_up();
        }
        LOG(INFO) << "Success attach " << logger->path;
        if(env.select_file){
            LOG(INFO) << "Start adding_target_file: ";
            bool has_inode = true;
            for(auto& id:env.inode){
                LOG(INFO) << "Trace_target_file_inode: " << id;
                int ret = bpf_map__update_elem(skel->maps.file_filter,&id,sizeof(unsigned long),&has_inode,sizeof(bool),BPF_NOEXIST);
                LOG_IF(FATAL,ret != 0) << "Fail to init file_trace";
            }
        }
    }
    ~Ext4DioSimpleModule(){}
};

int Ext4DioSimpleModule::proxy(void *ctx, void *data, size_t data_sz){
    switch (data_sz)
    {
        case sizeof(simple_dio_event):
            {
                DLOG(INFO) << "Getting simple_dio_event";
                int select = ext4_dio_sim_module->do_event(reinterpret_cast<struct simple_dio_event*>(data));
                DLOG_IF(INFO,select < 0) << "This event is dropped";
                ext4_dio_sim_module->do_meta(reinterpret_cast<struct simple_dio_event*>(data),select);
                break;
            }
        default:
            LOG(FATAL) << "Getting unknown type message, exit";
            break;
    }
    return 0;
}

int Ext4DioSimpleModule::__init_logger_normal(){
    struct file_with_format fwf_event = {
            .file_name = std::string("ext4_dio_simple_event"),
            .elems_size = std::vector<int>{8,8,8,8},
            .type_names = std::vector<std::string>{
                "end_dio_io",
                "kernel_crossing",
                "file_system",
                "block_io"
            }
    };
    fwf_event.row_size = accumulate(fwf_event.elems_size.begin(),fwf_event.elems_size.end(),0);
    logger->register_log(fwf_event);
    struct file_with_format fwf_meta = {
            .file_name = std::string("ext4_dio_simple_meta"),
            .elems_size = std::vector<int>{8,8,16,16},
            .type_names = std::vector<std::string>{
                "type",
                "dio_size",
                "common",
                "filename"
            }
    };
    fwf_meta.row_size = accumulate(fwf_meta.elems_size.begin(),fwf_meta.elems_size.end(),0);
    logger->register_log(fwf_meta);
    return 0;
}

int Ext4DioSimpleModule::do_event(const struct simple_dio_event* log){
    processed++;
    unsigned long long* event_data = new unsigned long long[4];
    //end_io
    event_data[0] = log->end_block_io;
    //kernel_crossing
    event_data[1] = log->start_ext4 - log->start_dio;
    //fs
    event_data[2] = log->start_block_io - log->start_ext4;
    //block
    event_data[3] = log->end_block_io - log->start_block_io;
    DLOG(INFO) << absl::StrFormat("%llu [%llu,%llu,%llu]",processed, event_data[1], event_data[2], event_data[3]);
    if(using_filter){
        unsigned int quantile_value = filter->quantile(filter_conf.quantile);
        DLOG(INFO) << absl::StrFormat("quantile at %f, value = %lu",filter_conf.quantile,quantile_value);
        int value = 0;
        switch (filter_conf.stage)
        {
            case SUM: 
                value = log->end_block_io - log->start_dio;
                break;
            case KERNEL_CROSSING:
                value = event_data[1];
                break;
            case FILE_SYSTEM:
                value = event_data[2];
                break;
            case BLOCK_IO:
                value = event_data[3];
                break;
            default:
                LOG(FATAL) << "Unknown filter stage selected";
                break;
        }
        filter->insert(value);
        if(value < quantile_value){
            //example: assume set p95
            //value is less than 95% latency just drop
            DLOG(INFO) << absl::StrFormat("%d dropped",processed);
            return -1;
        }
    }
    //do log commit
    std::string key = "ext4_dio_simple_event";
    int index = logger->fd_map[key];
    int length = logger->file_format[index].row_size;
    logger->commit(key,reinterpret_cast<char*>(event_data),length);
    delete[] event_data;
    return 1;
}

int Ext4DioSimpleModule::do_meta(const struct simple_dio_event* log,int select){
    //never to log, just commit to in-memory object
    dio_throughtput->commit(log->end_block_io,log->dio_size);
    const std::string dio_r_flag = "+dr";
    const std::string dio_w_flag = "+dw";
    std::string op_flag;
    switch (log->type)
    {
    case D_READ:
        op_flag = dio_r_flag;
        break;
    case D_WRITE:
        op_flag = dio_w_flag;
        break;
    default:
        LOG(FATAL) << "Unknown type of opeartion shoud be process";
        break;
    }
    auto file_name = std::string(log->filename);
    file_op->commit(file_name+op_flag,1);
    auto process_str = std::string(log->common);
    common_op->commit(process_str + op_flag,1);
    if(select == 1){
        DLOG(INFO) << "IO selected,write meta";
        unsigned long long* meta_data = new unsigned long long[6];
        meta_data[0] = log->type;
        meta_data[1] = log->dio_size;
        __builtin_memcpy(&meta_data[2],log->common,16);
        __builtin_memcpy(&meta_data[4],log->filename,16);
        std::string key = "ext4_dio_simple_meta";
        int index = logger->fd_map[key];
        int length = logger->file_format[index].row_size;
        logger->commit(key,reinterpret_cast<char*>(meta_data),length);
        delete[] meta_data;
        return 0;
    }
    DLOG(INFO) << "IO droped,only write meta to memory";
    return 0;
}

void Ext4DioSimpleModule::bpf_load_and_verify(struct Env env)
{
    this->skel = ext4_dio_simple_bpf__open();
    LOG_IF(FATAL,!skel) << absl::StrFormat("Failed to open BPF program: %s",strerror(errno));
    LOG(INFO) << "Loading ext4_dio_simple module ..... ";
    if(env.select_pid){
        skel->bss->tpid_ext4_dio = env.target_pid;
        skel->bss->using_pid_filter = 1;
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
        using_filter = true;
        this->filter_conf = env.filter_conf;
        filter = new digestible::Tdigest<unsigned int,unsigned int>(filter_conf.filter_accuracy);
    }
    LOG(INFO) << absl::StrFormat("Using filter with acc = %d",filter_conf.filter_accuracy);
        
    LOG(INFO) << "Creating meta_collecter :";
    
    LOG(INFO) << "dio_throughput";
    dio_throughtput = std::make_shared<TimeThroughputPmc>(env.pmc_inv,"dio_throughput");

    LOG(INFO) << "file operation counter";
    file_op = std::make_shared<Counter>("file_dio_op_counter");
    LOG(INFO) << "process operation counter";
    common_op = std::make_shared<Counter>("process_dio_op_counter");
    LOG(INFO) << "Verify ext4_dio_simple module ..... ";
    err = ext4_dio_simple_bpf__load(skel);
    if (err) {
        LOG(INFO) << "Failed to load and verify BPF skeleton";
        __clean_up();
    }
    LOG(INFO) << "Finish ext4_dio_simple load and verify";
}


