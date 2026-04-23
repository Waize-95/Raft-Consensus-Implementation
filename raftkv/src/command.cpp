#include<iostream>
#include<vector>
#include<string>
using namespace std;


struct Command{
    u_int8_t operation;
    u_int16_t key_len;
    u_int16_t value_len;
    string key;
    string value;
    Command():operation(0),key_len(0),value_len(0),key(""),value(""){};
};

vector<u_int8_t> Serialize(u_int8_t op,u_int16_t key_l,u_int16_t val_l, const string& _key,
const string& _value){
    vector<u_int8_t> result;
    result.reserve(5+key_l+val_l);

    result.push_back(op);
    u_int8_t key_high=static_cast<u_int8_t>((key_l>>8)&0xFF);
    result.push_back(key_high);
    u_int8_t key_low=static_cast<u_int8_t>(key_l&0xFF); 
    result.push_back(key_low);
    u_int8_t val_high=static_cast<u_int8_t>((val_l>>8)&0xFF);
    result.push_back(val_high);
    u_int8_t val_low=static_cast<u_int8_t>(val_l&0xFF);
    result.push_back(val_low);
    result.insert(result.end(),_key.begin(),_key.end());
    result.insert(result.end(),_value.begin(),_value.end());
    return result;
}

Command Deserialize(const vector<u_int8_t>& result){
    Command cmd;
    cmd.operation=result[0];
    u_int16_t key_len1=static_cast<u_int16_t>((result[1]<<8)|result[2]);
    u_int16_t val_len1=static_cast<u_int16_t>((result[3]<<8)|result[4]);
    cmd.key_len=key_len1;
    cmd.value_len=val_len1;
    cmd.key=string(result.begin()+5,result.begin()+5+key_len1);
    cmd.value=string(result.begin()+5+key_len1,result.begin()+5+key_len1+val_len1);
    return cmd;
}



// for serialize, converting the command struct into a byte vector 
// for deserialize, converting the byte vector back into a command struct
// may have to add the noop 
