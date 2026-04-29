#include<iostream>
#include<fstream>
#include<unistd.h>
#include<fcntl.h>
#include<sys/stat.h>
#include"log.cpp"
using namespace std;

// Data directory path (set by main before any storage calls)
string DATA_DIR = "./data";

struct MetaData{
    u_int64_t currentTerm;
    u_int64_t votedFor;
    u_int64_t commitIndex;
    u_int64_t lastApplied;
    MetaData():currentTerm(0),votedFor(0),commitIndex(0),lastApplied(0){}
};

// Initialize data directory and files if they don't exist
bool initStorage(){
    struct stat st;
    if(stat(DATA_DIR.c_str(), &st)!=0){
        if(mkdir(DATA_DIR.c_str(), 0755)!=0){
            cerr<<"Failed to create data directory: "<<DATA_DIR<<endl;
            return false;
        }
    }
    string meta_path = DATA_DIR + "/metadata.bin";
    if(stat(meta_path.c_str(), &st)!=0){
        MetaData initial;
        int fd = open(meta_path.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if(fd==-1) return false;
        write(fd, &initial, sizeof(initial));
        fsync(fd);
        close(fd);
    }
    string log_path = DATA_DIR + "/logs.bin";
    if(stat(log_path.c_str(), &st)!=0){
        int fd = open(log_path.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if(fd==-1) return false;
        close(fd);
    }
    return true;
}

bool updateMetaData(const MetaData& metadata){
    string path = DATA_DIR + "/metadata.bin";
    int descriptor=open(path.c_str(), O_WRONLY|O_CREAT| O_TRUNC, 0644);
    if(descriptor==-1){
        cerr<<"Couldn't open the metadata.bin file for writing"<<endl;
        return false;
    }
    ssize_t written_data=write(descriptor,&metadata,sizeof(metadata));
    if(written_data!=(ssize_t)sizeof(metadata)){
        cerr<<"Failed to write properly in the metadata.bin file"<<endl;
        close(descriptor);
        return false;
    }
    fsync(descriptor);
    close(descriptor);
    return true;
}

MetaData getMetaData(){
    MetaData tempmetadata;
    string path = DATA_DIR + "/metadata.bin";
    int descriptor=open(path.c_str(), O_RDONLY);
    if(descriptor==-1){
        // File doesn't exist yet — return defaults
        return MetaData();
    }
    ssize_t read_data=read(descriptor,&tempmetadata,sizeof(tempmetadata));
    if(read_data!=(ssize_t)sizeof(tempmetadata)){
        cerr<<"Failed to read properly from metadata.bin file"<<endl;
        close(descriptor);
        return MetaData();
    }
    close(descriptor);
    return tempmetadata;
}

//EVAL_HELP: you cannot store a variable length data structure directly into the
// file, you have to first convert it into raw bytes, hint->containts string(command)
bool writelog(const LogEntry& log){
    vector<u_int8_t>temp_arr;
    temp_arr=SerializeLogEntry(log);
    string path = DATA_DIR + "/logs.bin";
    int descriptor=open(path.c_str(), O_WRONLY| O_CREAT|O_APPEND,0644);
    if(descriptor==-1){
        cerr<<"Couldnt open the logs.bin file for writing";
        return false;
    }

    ssize_t written_logs=write(descriptor,temp_arr.data(),temp_arr.size());
    if(written_logs!=(ssize_t)temp_arr.size()){
        cerr<<"Failed to write properly in the logs.bin file"<<endl;
        close(descriptor);
        return false;
    }
    fsync(descriptor);
    close(descriptor);
    return true;
}

// Read all log entries from logs.bin, validating CRC on each entry.
// Returns the valid entries and sets valid_bytes for potential truncation.
vector<LogEntry> readLogs(size_t& valid_bytes){
    valid_bytes = 0;
    string path = DATA_DIR + "/logs.bin";
    int fd = open(path.c_str(), O_RDONLY);
    if(fd==-1){
        return {};
    }
    struct stat st;
    if(fstat(fd, &st)!=0){
        close(fd);
        return {};
    }
    if(st.st_size==0){
        close(fd);
        return {};
    }
    vector<u_int8_t> data(st.st_size);
    ssize_t bytes_read = read(fd, data.data(), st.st_size);
    close(fd);
    if(bytes_read!=st.st_size){
        cerr<<"Failed to read log file completely"<<endl;
        return {};
    }
    return DeserializeLogEntry(data, valid_bytes);
}

// Truncate log file to valid_bytes (discard partial entry at end after crash)
bool truncateLogFile(size_t valid_bytes){
    string path = DATA_DIR + "/logs.bin";
    if(truncate(path.c_str(), (off_t)valid_bytes)!=0){
        cerr<<"Failed to truncate log file"<<endl;
        return false;
    }
    return true;
}
