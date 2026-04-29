#include <iostream>
#include "command.cpp"

// CRC32 lookup table (standard polynomial 0xEDB88320)
static u_int32_t crc32_table[256];
static bool crc32_table_init = false;

void init_crc32_table(){
    for(u_int32_t i=0;i<256;i++){
        u_int32_t crc=i;
        for(int j=0;j<8;j++){
            if(crc & 1) crc=(crc>>1)^0xEDB88320;
            else crc>>=1;
        }
        crc32_table[i]=crc;
    }
    crc32_table_init=true;
}

u_int32_t computeCRC32(const vector<u_int8_t>& data){
    if(!crc32_table_init) init_crc32_table();
    u_int32_t crc=0xFFFFFFFF;
    for(u_int8_t byte : data){
        crc=(crc>>8)^crc32_table[(crc^byte)&0xFF];
    }
    return crc^0xFFFFFFFF;
}

struct LogEntry{
    u_int64_t term;
    u_int64_t index;
    Command  command;
    u_int32_t Checksum;

    LogEntry():term(0),index(0),command(),Checksum(0){}
    LogEntry(u_int64_t t,u_int64_t i,Command c):term(t),index(i),command(c),Checksum(0){}
    LogEntry(u_int64_t t,u_int64_t i,Command c,u_int32_t crc):term(t),index(i),command(c),Checksum(crc){}
};


vector<u_int8_t> SerializeLogEntry(const LogEntry& log){
    vector<u_int8_t> result;
    
    for(int i=0;i<8;i++){
        result.push_back((log.term >> (56 - i*8)) & 0xFF);
    }
    
    for(int i=0;i<8;i++){
        result.push_back((log.index >> (56 - i*8)) & 0xFF);
    }
    
    vector<u_int8_t> cmd = Serialize(log.command.operation,log.command.key_len,log.command.value_len,log.command.key,log.command.value);
    result.insert(result.end(), cmd.begin(), cmd.end());
    
    // Compute CRC32 over term + index + command bytes
    u_int32_t crc = computeCRC32(result);
    
    for(int i=0;i<4;i++){
        result.push_back((crc >> (24 - i*8)) & 0xFF);
    }
    return result;
}

// Deserializes all log entries from data. Sets valid_bytes to the number of
// bytes successfully parsed (for truncation of partial writes after a crash).
vector<LogEntry> DeserializeLogEntry(const vector<u_int8_t>& data, size_t& valid_bytes){
    vector<LogEntry> logs;
    size_t pos = 0;
    while(pos < data.size()){
        size_t entry_start = pos;

        // Need at least 16 bytes for term+index, 5 for min command, 4 for CRC = 25
        if(data.size() - pos < 25){
            break;
        }

        u_int64_t term = 0;
        for(int i=0;i<8;i++){
            term = (term << 8) | data[pos++];
        }
        
        u_int64_t index = 0;
        for(int i=0;i<8;i++){
            index = (index << 8) | data[pos++];
        }
        
        // Read key_len and value_len to know command size
        if(data.size() - pos < 5){
            pos = entry_start;
            break;
        }
        u_int16_t key_len = (data[pos+1]<<8)|data[pos+2];
        u_int16_t val_len = (data[pos+3]<<8)|data[pos+4];
        size_t cmd_size = 5 + key_len + val_len;
        
        if(data.size() - pos < cmd_size + 4){
            pos = entry_start;
            break;
        }
        
        Command cmd = Deserialize(vector<u_int8_t>(data.begin() + pos, data.begin() + pos + cmd_size));
        pos += cmd_size; 
        
        u_int32_t checksum = 0;
        for(int i=0;i<4;i++){
            checksum = (checksum << 8) | data[pos++];
        }
        
        // Validate CRC32
        vector<u_int8_t> check_data(data.begin()+entry_start, data.begin()+pos-4);
        u_int32_t computed = computeCRC32(check_data);
        if(checksum != computed){
            // CRC mismatch — partial/corrupt entry, stop here
            pos = entry_start;
            break;
        }
        
        logs.emplace_back(term, index, cmd, checksum);
    }
    valid_bytes = pos;
    return logs;
}