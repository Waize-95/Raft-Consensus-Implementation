#include<iostream>
#include<fstream>
#include<unistd.h>
#include<fcntl.h>
#include"log.cpp"
using namespace std;

struct MetaData{
    u_int64_t currentTerm;
    u_int64_t votedFor;
    u_int64_t logIndex;
    u_int64_t logTerm;
    MetaData():currentTerm(0),votedFor(0),logIndex(0),logTerm(0){}
};

bool updateMetaData(const MetaData& metadata){
    int descriptor=open("metadata.bin", O_WRONLY|O_CREAT| O_TRUNC, 0644);
    if(descriptor==-1){
        cerr<<"Couldn't open the metadata.bin file for writing"<<endl;
        return false;
    }
    ssize_t written_data=write(descriptor,&metadata,sizeof(metadata));
    if(written_data!=sizeof(metadata)){
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
    int descriptor=open("metadata.bin", O_RDONLY);
    if(descriptor==-1){
        cerr<<"Couldn't open the metadata.bin file for reading"<<endl;
        return MetaData();
    }
    ssize_t read_data=read(descriptor,&tempmetadata,sizeof(tempmetadata));
    if(read_data!=sizeof(tempmetadata)){
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
    int descriptor=open("logs.bin", O_WRONLY| O_CREAT|O_APPEND,0644);
    if(descriptor==-1){
        cerr<<"Couldnt open the logs.bin file for writing";
        return false;
    }

    ssize_t written_logs=write(descriptor,temp_arr.data(),temp_arr.size());
    if(written_logs!=sizeof(log)){
        cerr<<"Failed to write properly in the logs.bin file"<<endl;
        close(descriptor);
        return false;
    }
    fsync(descriptor);
    close(descriptor);
    return true;
}





