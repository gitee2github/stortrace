#ifndef _LOGGER_HPP_
#define _LOGGER_HPP_
#include "Env.hpp"
struct file_with_format{
    std::string file_name;
    std::vector<int> elems_size;
    std::vector<std::string> type_names;
    int row_size;
};
class Logger{
public:
    std::string path;
    std::vector<struct file_with_format> file_format;
    std::unordered_map<std::string,int> fd_map;
private:
    static std::string fwf_tostring(struct file_with_format fwf){
        std::string result;
        for(int i = 0;i < fwf.type_names.size();i++){
            result += fwf.type_names[i]+"--"+std::to_string(fwf.elems_size[i])+"|";
        }
        return result;
    }
    int* fd_set;
    int fd_index;
    std::vector<off_t> off_set_of;
    std::vector<int> file_commit_count;
    unsigned long long processed;
    struct io_uring ring;
    int SQ_depth;
private:
    int __dump_logger_meta(std::string log_data){
        std::string log_meta_path = path+"/"+"log_meta";
        int fd = open(log_meta_path.c_str(),O_RDWR | O_CREAT | O_TRUNC);
        if(fd){
            LOG(INFO) << absl::StrFormat("Creat log_meta: %s with fd %d",log_meta_path,fd);
        }else{
            LOG(FATAL) << absl::StrFormat("Failed creat log_meta %s",log_meta_path);
        }
        int length = log_data.length();
        char* buf = new char[length];
        memcpy(buf,log_data.c_str(),length);
        struct io_uring_sqe* sqe;
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_write(sqe,fd,buf,length,0);
        sqe->user_data = reinterpret_cast<unsigned long long>(buf);
        io_uring_submit(&ring);

        struct io_uring_sqe* sqe_finish;
        sqe_finish = io_uring_get_sqe(&ring);
        io_uring_prep_fsync(sqe_finish,fd,IORING_FSYNC_DATASYNC);
        sqe_finish->flags = IOSQE_IO_DRAIN;
        sqe_finish->user_data = INT16_MAX;
        
        io_uring_submit(&ring);
        LOG(INFO) << absl::string_view("doing fsync ......");
        struct io_uring_cqe* cqe;
        while(1){
            if(io_uring_peek_cqe(&ring,&cqe) == 0){
                if(cqe->user_data == INT16_MAX){
                    LOG(INFO) << absl::StrFormat("fsync finish %d",processed);
                    io_uring_cqe_seen(&ring,cqe);
                    break;
                }else{
                    if(cqe->user_data == 0){
                        //before sync
                    LOG(INFO) << absl::string_view("done 1 fsync");
                    io_uring_cqe_seen(&ring,cqe);
                    }else{
                        char* buf = reinterpret_cast<char*>(cqe->user_data);
                        if(buf){
                            delete buf;
                            processed++;
                            io_uring_cqe_seen(&ring,cqe);
                            LOG(INFO) << absl::StrFormat("delete buf %d",processed);
                        }
                    }
                }
            }
        }
        return 0;
    }

    int __flush_all(){
        std::string log_data;
        struct io_uring_sqe* sqe;
        if(fd_index == 0){
            LOG(INFO) << "None log commited to " << path << " logger";
        }
        for(int i = 0;i<fd_index;i++){
            log_data += file_format[i].file_name+" "+fwf_tostring(file_format[i])+" "+std::to_string(file_commit_count[i])+"\n";
            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_fsync(sqe,i,IORING_FSYNC_DATASYNC);
            sqe->flags = IOSQE_FIXED_FILE;
        }
        io_uring_submit(&ring);
        __dump_logger_meta(log_data);
        return 0;
    }
public:
    Logger(std::string p,int depth){
        fd_index = 0;
        path = p;
        processed = 0;
        mkdir(path.c_str(),0755);
        io_uring_queue_init(depth, &ring, 0);
    }
    int __register_to_io_uring(){
        fd_set = new int[file_format.size()];
        for(auto &item:file_format){
            std::string file_path = path + "/" + item.file_name;
            int fd = open(file_path.c_str(),O_RDWR | O_CREAT | O_TRUNC);
            LOG_IF(FATAL,fd < 0) << "Creat file: " << file_path << " failed";
            fd_map[item.file_name] = fd_index;
            fd_set[fd_index] = fd;
            fd_index++;
        }
        int n_fds = fd_index;
        off_set_of.resize(n_fds);
        file_commit_count.resize(n_fds);
        io_uring_register_files(&ring,fd_set,n_fds);
        return 0;
    }
    int register_log(struct file_with_format fwf){
        file_format.push_back(fwf);
        LOG(INFO) << "Register log file: " << fwf.file_name;
        LOG(INFO) << "The format of this log (type--size):";
        LOG(INFO) << fwf_tostring(fwf);
        LOG(INFO) << "Total row: " << fwf.row_size;
        return 0;
    }
    int commit(std::string f_name,const char* rec,int length){
        struct io_uring_sqe* sqe;
        sqe = io_uring_get_sqe(&ring);
        int index = fd_map[f_name];
        file_commit_count[index]++;
        char* send = new char[length];
        memcpy(send,rec,length);
        io_uring_prep_write(sqe,index,send,length,off_set_of[index]);
        off_set_of[index] += length;
        sqe->flags = IOSQE_FIXED_FILE;
        sqe->user_data = reinterpret_cast<unsigned long long>(send);
        io_uring_submit(&ring);
        DLOG(INFO) << "submit to " << f_name;
        struct io_uring_cqe* cqe;
        while(1){
            if(io_uring_peek_cqe(&ring,&cqe) == 0){
                char* buf = reinterpret_cast<char*>(cqe->user_data);
                if(buf){
                    delete buf;
                    processed++;
                    io_uring_cqe_seen(&ring,cqe);
                    DLOG(INFO) << absl::StrFormat("delete buf %llu",processed);
                }
            }else{
                break;
            }
        }
        return 0;
    }
    ~Logger(){
        __flush_all();
        io_uring_queue_exit(&ring);
        delete fd_set;
    }
};

static int register_log_from_fwf(struct file_with_format &fwf_event,Logger* logger){
    LOG(INFO) << fwf_event.elems_size.size() << " " << fwf_event.type_names.size();
    assert(fwf_event.elems_size.size() == fwf_event.type_names.size());
    fwf_event.row_size = accumulate(fwf_event.elems_size.begin(),fwf_event.elems_size.end(),0);
    logger->register_log(fwf_event);
    return 0;
}
#endif