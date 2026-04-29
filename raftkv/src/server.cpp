#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<poll.h>
#include<cstring>
#include<sstream>
#include<algorithm>
#include"statemachine.cpp"

// Phase 1: Single-node TCP server.
// Accepts client connections and handles PUT/GET/DELETE/\status/QUIT.
// In Phase 1, every command is committed immediately (no Raft protocol).
class RaftServer{
private:
    u_int64_t node_id;
    int port;
    KVStateMachine state_machine;
    MetaData metadata;
    vector<LogEntry> log_entries;

    // Parse a client command line and return the response
    string handleCommand(const string& line){
        // Trim trailing \r\n
        string trimmed = line;
        while(!trimmed.empty() && (trimmed.back()=='\n' || trimmed.back()=='\r'))
            trimmed.pop_back();

        if(trimmed.empty()) return "";

        // Parse command
        istringstream iss(trimmed);
        string cmd;
        iss >> cmd;

        // Convert to uppercase for comparison
        string upper_cmd = cmd;
        for(auto& c : upper_cmd) c = toupper(c);

        if(upper_cmd == "PUT"){
            string key, value;
            iss >> key >> value;
            if(key.empty()){
                return "ERR: PUT requires key and value\n";
            }
            if(value.empty()){
                return "ERR: PUT requires a value\n";
            }
            return handlePut(key, value);
        }else if(upper_cmd == "GET"){
            string key;
            iss >> key;
            if(key.empty()){
                return "ERR: GET requires a key\n";
            }
            return handleGet(key);
        }else if(upper_cmd == "DELETE"){
            string key;
            iss >> key;
            if(key.empty()){
                return "ERR: DELETE requires a key\n";
            }
            return handleDelete(key);
        }else if(trimmed == "\\status"){
            return handleStatus();
        }else if(upper_cmd == "QUIT"){
            return "QUIT";
        }else{
            return "ERR: unknown command '" + cmd + "'\n";
        }
    }

    string handlePut(const string& key, const string& value){
        // Create command
        Command cmd;
        cmd.operation = 1; // PUT
        cmd.key = key;
        cmd.value = value;
        cmd.key_len = key.size();
        cmd.value_len = value.size();

        // Create log entry
        u_int64_t new_index = log_entries.size() + 1;
        LogEntry entry(metadata.currentTerm, new_index, cmd);

        // Persist log entry to disk (fsync inside writelog)
        if(!writelog(entry)){
            return "ERR: failed to persist log entry\n";
        }

        // Add to in-memory log
        log_entries.push_back(entry);

        // Apply to state machine immediately (Phase 1: single node)
        state_machine.apply(cmd);

        // Update commitIndex and lastApplied
        metadata.commitIndex = new_index;
        metadata.lastApplied = new_index;

        // Persist metadata (fsync inside updateMetaData)
        if(!updateMetaData(metadata)){
            return "ERR: failed to persist metadata\n";
        }

        return "OK committed at index " + to_string(new_index) + " in term " +
               to_string(metadata.currentTerm) + "\n";
    }

    string handleDelete(const string& key){
        // Create command
        Command cmd;
        cmd.operation = 2; // DELETE
        cmd.key = key;
        cmd.value = "";
        cmd.key_len = key.size();
        cmd.value_len = 0;

        // Create log entry
        u_int64_t new_index = log_entries.size() + 1;
        LogEntry entry(metadata.currentTerm, new_index, cmd);

        // Persist log entry
        if(!writelog(entry)){
            return "ERR: failed to persist log entry\n";
        }

        log_entries.push_back(entry);
        state_machine.apply(cmd);

        metadata.commitIndex = new_index;
        metadata.lastApplied = new_index;
        if(!updateMetaData(metadata)){
            return "ERR: failed to persist metadata\n";
        }

        return "OK\n";
    }

    string handleGet(const string& key){
        // Direct lookup in state machine (no log entry for GET)
        if(state_machine.has(key)){
            return state_machine.get(key) + "\n";
        }
        return "NOT_FOUND\n";
    }

    string handleStatus(){
        string result;
        result += "node_id:      " + to_string(node_id) + "\n";
        result += "state:        LEADER\n";
        result += "term:         " + to_string(metadata.currentTerm) + "\n";
        result += "commitIndex:  " + to_string(metadata.commitIndex) + "\n";
        result += "lastApplied:  " + to_string(metadata.lastApplied) + "\n";
        result += "log length:   " + to_string(log_entries.size()) + "\n";
        return result;
    }

public:
    RaftServer(u_int64_t id, int p):node_id(id),port(p){}

    // Run startup recovery: load metadata, replay log into state machine
    bool recover(){
        // Initialize storage (create dirs/files if needed)
        if(!initStorage()){
            cerr<<"[node "<<node_id<<"] failed to initialize storage"<<endl;
            return false;
        }

        // Load metadata
        metadata = getMetaData();

        // Load log entries (with CRC validation)
        size_t valid_bytes;
        log_entries = readLogs(valid_bytes);

        // If there was a partial write, truncate the log file
        string log_path = DATA_DIR + "/logs.bin";
        struct stat st;
        if(stat(log_path.c_str(), &st)==0){
            if((size_t)st.st_size > valid_bytes){
                cerr<<"[node "<<node_id<<"] truncating corrupted log tail ("
                    <<st.st_size<<" -> "<<valid_bytes<<" bytes)"<<endl;
                truncateLogFile(valid_bytes);
            }
        }

        // Replay committed entries into the state machine
        for(size_t i=0; i<log_entries.size() && (i+1)<=(size_t)metadata.lastApplied; i++){
            state_machine.apply(log_entries[i].command);
        }

        // If metadata shows fewer entries than log (crash before metadata update),
        // apply remaining entries and fix metadata
        if(log_entries.size() > metadata.lastApplied){
            for(size_t i=metadata.lastApplied; i<log_entries.size(); i++){
                state_machine.apply(log_entries[i].command);
            }
            metadata.commitIndex = log_entries.size();
            metadata.lastApplied = log_entries.size();
            updateMetaData(metadata);
        }

        string voted = (metadata.votedFor==0) ? "none" : to_string(metadata.votedFor);
        cout<<"[node "<<node_id<<"] persistent state loaded: currentTerm="
            <<metadata.currentTerm<<" votedFor="<<voted
            <<" log=["<<log_entries.size()<<" entries]"<<endl;

        return true;
    }

    // Start the TCP server (blocking event loop using poll)
    void run(){
        // Create listening socket
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){
            cerr<<"[node "<<node_id<<"] failed to create socket"<<endl;
            return;
        }

        // Allow port reuse
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if(bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            cerr<<"[node "<<node_id<<"] failed to bind to port "<<port<<endl;
            close(listen_fd);
            return;
        }

        if(listen(listen_fd, 5) < 0){
            cerr<<"[node "<<node_id<<"] failed to listen"<<endl;
            close(listen_fd);
            return;
        }

        cout<<"[node "<<node_id<<"] ready for client commands on port "<<port<<endl;

        // Poll-based event loop
        // pollfd[0] = listening socket, rest = client connections
        vector<struct pollfd> fds;
        vector<string> client_buffers; // per-client read buffers

        struct pollfd listen_pfd;
        listen_pfd.fd = listen_fd;
        listen_pfd.events = POLLIN;
        listen_pfd.revents = 0;
        fds.push_back(listen_pfd);
        client_buffers.push_back(""); // placeholder for listen socket

        while(true){
            int ret = poll(fds.data(), fds.size(), -1);
            if(ret < 0){
                cerr<<"[node "<<node_id<<"] poll error"<<endl;
                break;
            }

            // Check listening socket for new connections
            if(fds[0].revents & POLLIN){
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
                if(client_fd >= 0){
                    cout<<"[node "<<node_id<<"] client connected"<<endl;
                    struct pollfd cpfd;
                    cpfd.fd = client_fd;
                    cpfd.events = POLLIN;
                    cpfd.revents = 0;
                    fds.push_back(cpfd);
                    client_buffers.push_back("");
                }
            }

            // Check client sockets
            for(size_t i=1; i<fds.size(); ){
                if(fds[i].revents & (POLLIN | POLLHUP | POLLERR)){
                    char buf[4096];
                    ssize_t n = recv(fds[i].fd, buf, sizeof(buf)-1, 0);
                    if(n <= 0){
                        // Client disconnected
                        cout<<"[node "<<node_id<<"] client disconnected"<<endl;
                        close(fds[i].fd);
                        fds.erase(fds.begin()+i);
                        client_buffers.erase(client_buffers.begin()+i);
                        continue;
                    }

                    buf[n] = '\0';
                    client_buffers[i] += string(buf, n);

                    // Process complete lines
                    size_t newline_pos;
                    while((newline_pos = client_buffers[i].find('\n')) != string::npos){
                        string line = client_buffers[i].substr(0, newline_pos+1);
                        client_buffers[i] = client_buffers[i].substr(newline_pos+1);

                        string response = handleCommand(line);

                        if(response == "QUIT"){
                            string bye = "Goodbye\n";
                            send(fds[i].fd, bye.c_str(), bye.size(), 0);
                            cout<<"[node "<<node_id<<"] client quit"<<endl;
                            close(fds[i].fd);
                            fds.erase(fds.begin()+i);
                            client_buffers.erase(client_buffers.begin()+i);
                            goto next_poll;
                        }

                        if(!response.empty()){
                            send(fds[i].fd, response.c_str(), response.size(), 0);
                        }
                    }
                }
                i++;
                next_poll:;
            }
        }

        close(listen_fd);
    }
};
