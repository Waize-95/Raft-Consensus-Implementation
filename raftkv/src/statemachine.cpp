#include<unordered_map>
#include"storage.cpp"

// The key-value state machine.
// Completely deterministic: given the same sequence of committed entries,
// every server must arrive at the same key-value state.
class KVStateMachine{
private:
    unordered_map<string,string> store;

public:
    // Apply a command to the state machine
    //   PUT (1)    -> inserts or overwrites the key
    //   DELETE (2) -> removes the key
    //   NOOP (3)   -> does nothing
    void apply(const Command& cmd){
        if(cmd.operation==1){
            // PUT
            store[cmd.key]=cmd.value;
        }else if(cmd.operation==2){
            // DELETE
            store.erase(cmd.key);
        }
        // NOOP (3) does nothing
    }

    // Get the value for a key. Returns empty string if not found.
    string get(const string& key) const{
        auto it=store.find(key);
        if(it!=store.end()){
            return it->second;
        }
        return "";
    }

    // Check if a key exists
    bool has(const string& key) const{
        return store.find(key)!=store.end();
    }

    // Get all key-value pairs (useful for debugging)
    const unordered_map<string,string>& getAll() const{
        return store;
    }
};
