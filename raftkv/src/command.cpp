#include<iostream>
#include<vector>
using namespace std;


struct Command{
    u_int8_t operation;
    u_int16_t key_len;
    u_int16_t value_len;
    string key;
    string value;
};

vector<u_int8_t> Serialize(u_int8_t op,u_int16_t key_l,u_int8_t val_l, string key,
string _value){

    vector<u_int8_t> result;
    


}