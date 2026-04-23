#include<iostream>
#include<fstream>
#include<unistd.h>
#include<fcntl.h>
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



