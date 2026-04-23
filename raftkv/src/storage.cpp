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
    MetaData():currentTerm(0),votedFor(0){}
};

bool updateMetaData(const MetaData& metadata){
    ofstream file("metadata.bin",ios::out |ios::trunc | ios::binary);
    if(!file.is_open()){
        cerr<<"Failed to open metadata file for writing"<<endl;
        return false;
    }
    file.write(reinterpret_cast<const char*>(&metadata),sizeof(metadata));
    file.close();
    // fsync();
    return true;
}


