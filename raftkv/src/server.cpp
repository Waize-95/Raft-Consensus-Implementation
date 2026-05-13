#ifndef SERVER_CPP
#define SERVER_CPP

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<poll.h>
#include<cstring>
#include<sstream>
#include<algorithm>
#include<mutex>
#include<thread>
#include<atomic>
#include<chrono>
#include<random>
#include"statemachine.cpp"
#include"rcp.cpp"

// Raft node roles
enum Role { FOLLOWER, CANDIDATE, LEADER };

string roleToString(Role r){
    switch(r){
        case FOLLOWER:  return "FOLLOWER";
        case CANDIDATE: return "CANDIDATE";
        case LEADER:    return "LEADER";
        default:        return "UNKNOWN";
    }
}

// ============================================================
// Raft TCP server with background threads for elections
// and heartbeats (Phase 2, Step 4).
// ============================================================
class RaftServer{
private:
    // === Core Raft state ===
    u_int64_t node_id;
    int port;          // client-facing port (text protocol)
    int peer_port;     // peer-facing port (binary RPC protocol)
    Role role;
    vector<pair<string,int>> peers;  // peer RPC addresses (host, peer_port)
    std::mutex raft_mutex;           // protects ALL shared state
    KVStateMachine state_machine;
    MetaData metadata;
    vector<LogEntry> log_entries;

    // === Threading and timer state ===
    std::atomic<bool> running;       // controls background thread lifetime
    std::chrono::steady_clock::time_point election_deadline;
    uint64_t votes_received;         // votes in current election
    std::mt19937 rng;                // per-node RNG for election timeouts

    // ============================================================
    // Helper: reset election timer to random [300, 500] ms.
    // MUST be called with raft_mutex held.
    // ============================================================
    void resetElectionTimer(){
        std::uniform_int_distribution<int> dist(300, 500);
        int timeout_ms = dist(rng);
        election_deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(timeout_ms);
    }

    // ============================================================
    // Helpers: last log index / term.  Must hold raft_mutex.
    // ============================================================
    uint64_t getLastLogIndex(){
        return log_entries.empty() ? 0 : log_entries.back().index;
    }
    uint64_t getLastLogTerm(){
        return log_entries.empty() ? 0 : log_entries.back().term;
    }

    // ============================================================
    // Helper: step down to FOLLOWER on higher term.
    // Must hold raft_mutex.  Persists (fsync) before returning.
    // ============================================================
    void stepDown(uint64_t newTerm){
        role = FOLLOWER;
        metadata.currentTerm = newTerm;
        metadata.votedFor = 0;
        updateMetaData(metadata);
    }

    // ============================================================
    // Open a TCP connection to a peer.  Returns fd or -1.
    // ============================================================
    int connectToPeer(const string& host, int pport){
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0) return -1;

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(pport);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            close(fd);
            return -1;
        }
        return fd;
    }

    // ============================================================
    // RequestVote handler (Section 7.1).  Acquires raft_mutex.
    // ============================================================
    RequestVoteReply handleRequestVote(const RequestVoteArgs& args){
        std::lock_guard<std::mutex> lock(raft_mutex);

        RequestVoteReply reply;
        reply.term = metadata.currentTerm;
        reply.voteGranted = false;

        // Reject if candidate's term < ours
        if(args.term < metadata.currentTerm){
            return reply;
        }

        // Higher term → step down
        if(args.term > metadata.currentTerm){
            stepDown(args.term);
        }
        reply.term = metadata.currentTerm;

        // Can we vote?
        bool canVote = (metadata.votedFor == 0 ||
                        metadata.votedFor == args.candidateId);

        // Log up-to-date check (§5.4.1)
        uint64_t ourLastTerm  = getLastLogTerm();
        uint64_t ourLastIndex = getLastLogIndex();
        bool logOk = (args.lastLogTerm > ourLastTerm) ||
                     (args.lastLogTerm == ourLastTerm &&
                      args.lastLogIndex >= ourLastIndex);

        if(canVote && logOk){
            reply.voteGranted = true;
            metadata.votedFor = args.candidateId;
            updateMetaData(metadata);  // fsync before reply
            resetElectionTimer();

            cout << "[node " << node_id << "] voted for node "
                 << args.candidateId << " in term "
                 << metadata.currentTerm << endl;
        }
        return reply;
    }

    // ============================================================
    // AppendEntries handler (§7.2 Steps 1-3, heartbeat case).
    // Acquires raft_mutex.
    // ============================================================
    AppendEntriesReply handleAppendEntries(const AppendEntriesArgs& args){
        std::lock_guard<std::mutex> lock(raft_mutex);

        AppendEntriesReply reply;
        reply.term = metadata.currentTerm;
        reply.success = false;

        // Step 1: reject if leader's term < ours
        if(args.term < metadata.currentTerm){
            return reply;
        }

        // Higher term → update
        if(args.term > metadata.currentTerm){
            metadata.currentTerm = args.term;
            metadata.votedFor = 0;
            role = FOLLOWER;
            updateMetaData(metadata);
        }

        // Candidate → step down on receiving AppendEntries
        if(role == CANDIDATE){
            role = FOLLOWER;
            cout << "[node " << node_id
                 << "] stepping down CANDIDATE→FOLLOWER (AppendEntries from "
                 << args.leaderId << " term " << args.term << ")" << endl;
        }
        reply.term = metadata.currentTerm;

        // Step 2: log consistency check
        if(args.prevLogIndex > 0){
            if(args.prevLogIndex > (uint64_t)log_entries.size()){
                return reply;
            }
            if(log_entries[args.prevLogIndex - 1].term != args.prevLogTerm){
                return reply;
            }
        }

        // Step 3: conflict detection (no-op for empty entries / heartbeats)

        // Valid heartbeat → reset election timer
        resetElectionTimer();
        reply.success = true;
        return reply;
    }

    // ============================================================
    // Handle one incoming peer connection (one-shot RPC).
    // Runs in a detached thread.
    // ============================================================
    void handlePeerConnection(int fd){
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        RpcType type;
        vector<uint8_t> body;
        if(!recvRpc(fd, type, body)){
            close(fd);
            return;
        }

        if(type == RPC_REQUEST_VOTE_ARGS){
            auto args  = deserializeRequestVoteArgs(body.data());
            auto reply = handleRequestVote(args);
            auto rbody = serializeRequestVoteReply(reply);
            sendRpc(fd, RPC_REQUEST_VOTE_REPLY, rbody);
        }
        else if(type == RPC_APPEND_ENTRIES_ARGS){
            auto args  = deserializeAppendEntriesArgs(body.data(),
                                                       (uint32_t)body.size());
            auto reply = handleAppendEntries(args);
            auto rbody = serializeAppendEntriesReply(reply);
            sendRpc(fd, RPC_APPEND_ENTRIES_REPLY, rbody);
        }

        close(fd);
    }

    // ============================================================
    // Broadcast RequestVote to all peers (detached threads).
    // Called WITHOUT raft_mutex held.
    // ============================================================
    void sendRequestVoteToAll(){
        std::unique_lock<std::mutex> lock(raft_mutex);
        RequestVoteArgs args;
        args.term         = metadata.currentTerm;
        args.candidateId  = node_id;
        args.lastLogIndex = getLastLogIndex();
        args.lastLogTerm  = getLastLogTerm();
        uint64_t electionTerm = metadata.currentTerm;
        auto peer_copy = peers;
        lock.unlock();

        for(auto& peer : peer_copy){
            std::thread([this, peer, args, electionTerm](){
                int fd = connectToPeer(peer.first, peer.second);
                if(fd < 0) return;

                auto body = serializeRequestVoteArgs(args);
                if(!sendRpc(fd, RPC_REQUEST_VOTE_ARGS, body)){
                    close(fd); return;
                }

                RpcType rt;
                vector<uint8_t> rb;
                if(!recvRpc(fd, rt, rb)){
                    close(fd); return;
                }
                close(fd);

                if(rt != RPC_REQUEST_VOTE_REPLY || rb.size() < 9) return;
                auto reply = deserializeRequestVoteReply(rb.data());

                std::lock_guard<std::mutex> lk(raft_mutex);

                if(reply.term > metadata.currentTerm){
                    stepDown(reply.term);
                    return;
                }
                if(role != CANDIDATE || metadata.currentTerm != electionTerm)
                    return;

                if(reply.voteGranted){
                    votes_received++;
                    size_t majority = (peers.size() + 1) / 2 + 1;
                    if(votes_received >= majority){
                        role = LEADER;
                        cout << "[node " << node_id
                             << "] became LEADER for term "
                             << metadata.currentTerm << " with "
                             << votes_received << " votes" << endl;
                    }
                }
            }).detach();
        }
    }

    // ============================================================
    // Broadcast empty AppendEntries (heartbeat) to all peers.
    // Called WITHOUT raft_mutex held.
    // ============================================================
    void sendAppendEntriesToAll(){
        std::unique_lock<std::mutex> lock(raft_mutex);
        if(role != LEADER) return;

        AppendEntriesArgs args;
        args.term         = metadata.currentTerm;
        args.leaderId     = node_id;
        args.prevLogIndex = getLastLogIndex();
        args.prevLogTerm  = getLastLogTerm();
        args.leaderCommit = metadata.commitIndex;
        auto peer_copy = peers;
        lock.unlock();

        for(auto& peer : peer_copy){
            std::thread([this, peer, args](){
                int fd = connectToPeer(peer.first, peer.second);
                if(fd < 0) return;

                auto body = serializeAppendEntriesArgs(args);
                if(!sendRpc(fd, RPC_APPEND_ENTRIES_ARGS, body)){
                    close(fd); return;
                }

                RpcType rt;
                vector<uint8_t> rb;
                if(!recvRpc(fd, rt, rb)){
                    close(fd); return;
                }
                close(fd);

                if(rt != RPC_APPEND_ENTRIES_REPLY || rb.size() < 9) return;
                auto reply = deserializeAppendEntriesReply(rb.data());

                std::lock_guard<std::mutex> lk(raft_mutex);
                if(reply.term > metadata.currentTerm){
                    stepDown(reply.term);
                }
            }).detach();
        }
    }

    // ============================================================
    // Background: RPC Receiver Loop
    // Listens on peer_port, accepts connections, dispatches RPCs.
    // ============================================================
    void rpcReceiverLoop(){
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){
            cerr << "[node " << node_id
                 << "] rpcReceiver: socket failed" << endl;
            return;
        }

        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(peer_port);

        if(bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            cerr << "[node " << node_id
                 << "] rpcReceiver: bind failed on port "
                 << peer_port << endl;
            close(listen_fd);
            return;
        }

        if(listen(listen_fd, 16) < 0){
            cerr << "[node " << node_id
                 << "] rpcReceiver: listen failed" << endl;
            close(listen_fd);
            return;
        }

        cout << "[node " << node_id
             << "] listening for peer RPCs on port "
             << peer_port << endl;

        while(running){
            struct pollfd pfd;
            pfd.fd = listen_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            int ret = poll(&pfd, 1, 200);
            if(ret <= 0) continue;

            struct sockaddr_in pa;
            socklen_t pl = sizeof(pa);
            int pfd2 = accept(listen_fd, (struct sockaddr*)&pa, &pl);
            if(pfd2 < 0) continue;

            std::thread(&RaftServer::handlePeerConnection, this, pfd2).detach();
        }
        close(listen_fd);
    }

    // ============================================================
    // Background: Election Timer Loop
    // Checks every 20ms.  On timeout → CANDIDATE.
    // ============================================================
    void electionTimerLoop(){
        while(running){
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            std::unique_lock<std::mutex> lock(raft_mutex);
            if(!running) break;
            if(role == LEADER) continue;

            auto now = std::chrono::steady_clock::now();
            if(now < election_deadline) continue;

            // === Election timeout ===
            role = CANDIDATE;
            metadata.currentTerm++;
            metadata.votedFor = node_id;
            votes_received = 1;
            updateMetaData(metadata);

            cout << "[node " << node_id
                 << "] election timeout -> CANDIDATE term "
                 << metadata.currentTerm << endl;

            resetElectionTimer();
            lock.unlock();

            sendRequestVoteToAll();
        }
    }

    // ============================================================
    // Background: Heartbeat Timer Loop (Leader only)
    // Every 100ms, broadcast empty AppendEntries.
    // ============================================================
    void heartbeatTimerLoop(){
        while(running){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> lock(raft_mutex);
                if(!running) break;
                if(role != LEADER) continue;
            }
            sendAppendEntriesToAll();
        }
    }

    // ============================================================
    // Client command handler (preserved from Phase 1)
    // ============================================================
    string handleCommand(const string& line){
        string trimmed = line;
        while(!trimmed.empty() &&
              (trimmed.back()=='\n' || trimmed.back()=='\r'))
            trimmed.pop_back();
        if(trimmed.empty()) return "";

        istringstream iss(trimmed);
        string cmd;
        iss >> cmd;
        string upper_cmd = cmd;
        for(auto& c : upper_cmd) c = toupper(c);

        std::lock_guard<std::mutex> lock(raft_mutex);

        if(upper_cmd == "PUT"){
            string key, value;
            iss >> key >> value;
            if(key.empty()) return "ERR: PUT requires key and value\n";
            if(value.empty()) return "ERR: PUT requires a value\n";
            return handlePut(key, value);
        }else if(upper_cmd == "GET"){
            string key;
            iss >> key;
            if(key.empty()) return "ERR: GET requires a key\n";
            return handleGet(key);
        }else if(upper_cmd == "DELETE"){
            string key;
            iss >> key;
            if(key.empty()) return "ERR: DELETE requires a key\n";
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
        (void)key; (void)value;
        return "ERR: log replication is not yet implemented\n";
    }

    string handleDelete(const string& key){
        (void)key;
        return "ERR: log replication is not yet implemented\n";
    }

    string handleGet(const string& key){
        if(state_machine.has(key))
            return state_machine.get(key) + "\n";
        return "NOT_FOUND\n";
    }

    string handleStatus(){
        string result;
        result += "node_id:      " + to_string(node_id) + "\n";
        result += "state:        " + roleToString(role) + "\n";
        result += "term:         " + to_string(metadata.currentTerm) + "\n";
        result += "commitIndex:  " + to_string(metadata.commitIndex) + "\n";
        result += "lastApplied:  " + to_string(metadata.lastApplied) + "\n";
        result += "log length:   " + to_string(log_entries.size()) + "\n";
        if(role == LEADER){
            result += "peers:        ";
            if(peers.empty()){
                result += "(none)";
            }else{
                for(size_t i=0; i<peers.size(); i++){
                    if(i>0) result += ", ";
                    result += peers[i].first + ":" + to_string(peers[i].second);
                }
            }
            result += "\n";
        }
        return result;
    }

public:
    // Constructor: client port + peer RPC port
    RaftServer(u_int64_t id, int p, int pp,
               const vector<pair<string,int>>& peer_list = {})
        : node_id(id), port(p), peer_port(pp),
          role(FOLLOWER), peers(peer_list),
          running(false), votes_received(0),
          rng(std::random_device{}() ^ (unsigned)id) {}

    // Recovery: load metadata, replay log
    bool recover(){
        if(!initStorage()){
            cerr << "[node " << node_id
                 << "] failed to initialize storage" << endl;
            return false;
        }

        metadata = getMetaData();

        size_t valid_bytes;
        log_entries = readLogs(valid_bytes);

        string log_path = DATA_DIR + "/logs.bin";
        struct stat st;
        if(stat(log_path.c_str(), &st)==0){
            if((size_t)st.st_size > valid_bytes){
                cerr << "[node " << node_id
                     << "] truncating corrupted log tail ("
                     << st.st_size << " -> " << valid_bytes
                     << " bytes)" << endl;
                truncateLogFile(valid_bytes);
            }
        }

        for(size_t i=0;
            i<log_entries.size() && (i+1)<=(size_t)metadata.lastApplied;
            i++){
            state_machine.apply(log_entries[i].command);
        }

        if(log_entries.size() > metadata.lastApplied){
            for(size_t i=metadata.lastApplied; i<log_entries.size(); i++){
                state_machine.apply(log_entries[i].command);
            }
            metadata.commitIndex = log_entries.size();
            metadata.lastApplied = log_entries.size();
            updateMetaData(metadata);
        }

        string voted = (metadata.votedFor==0)
                        ? "none" : to_string(metadata.votedFor);
        cout << "[node " << node_id
             << "] persistent state loaded: currentTerm="
             << metadata.currentTerm << " votedFor=" << voted
             << " log=[" << log_entries.size() << " entries]"
             << endl;

        return true;
    }

    // ============================================================
    // Run: launch background threads, then enter client event loop.
    // ============================================================
    void run(){
        running = true;

        // Initialize election timer
        {
            std::lock_guard<std::mutex> lock(raft_mutex);
            resetElectionTimer();
        }

        // Launch background threads
        std::thread rpc_thread(
            &RaftServer::rpcReceiverLoop, this);
        std::thread election_thread(
            &RaftServer::electionTimerLoop, this);
        std::thread heartbeat_thread(
            &RaftServer::heartbeatTimerLoop, this);

        // === Client TCP server (text protocol) ===
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){
            cerr << "[node " << node_id
                 << "] failed to create client socket" << endl;
            running = false;
            rpc_thread.join();
            election_thread.join();
            heartbeat_thread.join();
            return;
        }

        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if(bind(listen_fd, (struct sockaddr*)&addr,
                sizeof(addr)) < 0){
            cerr << "[node " << node_id
                 << "] failed to bind client port " << port << endl;
            close(listen_fd);
            running = false;
            rpc_thread.join();
            election_thread.join();
            heartbeat_thread.join();
            return;
        }

        if(listen(listen_fd, 5) < 0){
            cerr << "[node " << node_id
                 << "] failed to listen on client port" << endl;
            close(listen_fd);
            running = false;
            rpc_thread.join();
            election_thread.join();
            heartbeat_thread.join();
            return;
        }

        cout << "[node " << node_id
             << "] ready for client commands on port "
             << port << endl;

        vector<struct pollfd> fds;
        vector<string> client_buffers;

        struct pollfd listen_pfd;
        listen_pfd.fd = listen_fd;
        listen_pfd.events = POLLIN;
        listen_pfd.revents = 0;
        fds.push_back(listen_pfd);
        client_buffers.push_back("");

        while(running){
            int ret = poll(fds.data(), fds.size(), 500);
            if(ret < 0){
                cerr << "[node " << node_id
                     << "] poll error" << endl;
                break;
            }
            if(ret == 0) continue;

            if(fds[0].revents & POLLIN){
                struct sockaddr_in ca;
                socklen_t cl = sizeof(ca);
                int cfd = accept(listen_fd,
                                 (struct sockaddr*)&ca, &cl);
                if(cfd >= 0){
                    cout << "[node " << node_id
                         << "] client connected" << endl;
                    struct pollfd cpfd;
                    cpfd.fd = cfd;
                    cpfd.events = POLLIN;
                    cpfd.revents = 0;
                    fds.push_back(cpfd);
                    client_buffers.push_back("");
                }
            }

            for(size_t i=1; i<fds.size(); ){
                if(fds[i].revents &
                   (POLLIN | POLLHUP | POLLERR)){
                    char buf[4096];
                    ssize_t n = recv(fds[i].fd, buf,
                                     sizeof(buf)-1, 0);
                    if(n <= 0){
                        cout << "[node " << node_id
                             << "] client disconnected" << endl;
                        close(fds[i].fd);
                        fds.erase(fds.begin()+i);
                        client_buffers.erase(
                            client_buffers.begin()+i);
                        continue;
                    }

                    buf[n] = '\0';
                    client_buffers[i] += string(buf, n);

                    size_t nlp;
                    while((nlp = client_buffers[i].find('\n'))
                          != string::npos){
                        string line =
                            client_buffers[i].substr(0, nlp+1);
                        client_buffers[i] =
                            client_buffers[i].substr(nlp+1);

                        string response = handleCommand(line);

                        if(response == "QUIT"){
                            string bye = "Goodbye\n";
                            send(fds[i].fd, bye.c_str(),
                                 bye.size(), 0);
                            cout << "[node " << node_id
                                 << "] client quit" << endl;
                            close(fds[i].fd);
                            fds.erase(fds.begin()+i);
                            client_buffers.erase(
                                client_buffers.begin()+i);
                            goto next_poll;
                        }

                        if(!response.empty()){
                            send(fds[i].fd, response.c_str(),
                                 response.size(), 0);
                        }
                    }
                }
                i++;
                next_poll:;
            }
        }

        close(listen_fd);
        rpc_thread.join();
        election_thread.join();
        heartbeat_thread.join();
    }
};

#endif // SERVER_CPP
