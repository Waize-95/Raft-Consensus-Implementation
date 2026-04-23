#include<iostream>
#include<fstream>
using namespace std;

struct MetaData{
    u_int64_t currentTerm;
    u_int64_t votedFor;
    u_int64_t logIndex;
    u_int64_t logTerm;
    MetaData():currentTerm(0),votedFor(0){}
};

bool updateMetaData(const MetaData& metadata){
    ofstream out("metadata.bin",ios::out |ios::app | ios::binary);
    if(!out.is_open()){
        cerr<<"Failed to open metadata file for writing"<<endl;
        return false;
    }


}