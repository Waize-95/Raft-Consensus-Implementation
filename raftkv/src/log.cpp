#include <iostream>
#include "command.cpp"

struct LogEntry{
    u_int64_t term;
    u_int64_t index;
    Command  command;
    u_int32_t Checksum;

    LogEntry(u_int64_t t,u_int64_t i,Command c):term(t),index(i),command(c){
        Checksum = 0;
    }
};


vector<u_int8_t> SerializeLogEntry(LogEntry log){
    vector<u_int8_t> result;
    
    for(int i=0;i<8;i++){
        result.push_back((log.term >> (56 - i*8)) & 0xFF);
    }
    
    for(int i=0;i<8;i++){
        result.push_back((log.index >> (56 - i*8)) & 0xFF);
    }
    
    vector<u_int8_t> cmd = Serialize(log.command.operation,log.command.key_len,log.command.value_len,log.command.key,log.command.value);
    result.insert(result.end(), cmd.begin(), cmd.end());
    
    for(int i=0;i<4;i++){
        result.push_back((log.Checksum >> (24 - i*8)) & 0xFF);
    }
    return result;
}

vector<LogEntry> DeserializeLogEntry(const vector<u_int8_t>& data){
    vector<LogEntry> logs;
    size_t pos = 0;
    while(pos < data.size()){

        u_int64_t term = 0;
        for(int i=0;i<8;i++){
            term = (term << 8) | data[pos++];
        }
        
        u_int64_t index = 0;
        for(int i=0;i<8;i++){
            index = (index << 8) | data[pos++];
        }
        
        Command cmd =  Deserialize(vector<u_int8_t>(data.begin() + pos, data.end()));
        pos += 5 + cmd.key_len + cmd.value_len; 
        
        u_int32_t checksum = 0;
        for(int i=0;i<4;i++){
            checksum = (checksum << 8) | data[pos++];
        }
        
        logs.emplace_back(term, index, cmd);
    }
    return logs;
}