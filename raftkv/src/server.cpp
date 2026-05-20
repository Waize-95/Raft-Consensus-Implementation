#ifndef SERVER_CPP
#define SERVER_CPP

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<poll.h>
#include<cstring>
#include<cerrno>
#include<sstream>
#include<algorithm>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<atomic>
#include<chrono>
#include<random>
#include<map>
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

// Command op codes (mirror statemachine.cpp)
static const u_int8_t OP_PUT    = 1;
static const u_int8_t OP_DELETE = 2;
static const u_int8_t OP_NOOP   = 3;

// ============================================================
// Full Raft node: leader election, log replication, commitment,
// and a key-value state machine on top (Phase 3).
// ============================================================
class RaftServer{
private:
    struct Peer { uint64_t id; string host; int port; };

    // === Core Raft state ===
    u_int64_t node_id;
    int port;          // client-facing port (text protocol)
    int peer_port;     // peer-facing port (binary RPC protocol)
    Role role;
    vector<Peer> peers;              // peer RPC addresses (id, host, peer_port)
    std::mutex raft_mutex;           // protects ALL shared state below
    KVStateMachine state_machine;
    MetaData metadata;               // currentTerm, votedFor, commitIndex, lastApplied
    vector<LogEntry> log_entries;    // log[1..N], 1-based index in entry.index

    // === Threading / timers ===
    std::atomic<bool> running;
    std::chrono::steady_clock::time_point election_deadline;
    uint64_t votes_received;
    std::mt19937 rng;
    uint64_t current_leader_id;      // last known leader (0 = unknown)

    // === Leader-only volatile state (rebuilt on each election) ===
    std::map<uint64_t,uint64_t> nextIndex;   // keyed by peer id
    std::map<uint64_t,uint64_t> matchIndex;  // keyed by peer id
    std::map<uint64_t,bool>     peerUp;       // last contact succeeded?

    // === Coordination ===
    std::condition_variable replicate_cv;    // kicks per-peer sender threads
    std::condition_variable commit_cv;       // wakes client threads waiting on commit

    // ------------------------------------------------------------
    size_t majority(){ return (peers.size() + 1) / 2 + 1; }

    void resetElectionTimer(){
        std::uniform_int_distribution<int> dist(300, 500);
        election_deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(dist(rng));
    }

    uint64_t getLastLogIndex(){
        return log_entries.empty() ? 0 : log_entries.back().index;
    }
    uint64_t getLastLogTerm(){
        return log_entries.empty() ? 0 : log_entries.back().term;
    }
    // term of entry at 1-based index (0 for sentinel / out of range)
    uint64_t termAt(uint64_t index){
        if(index == 0 || index > log_entries.size()) return 0;
        return log_entries[index - 1].term;
    }

    // Persist whole log (used after truncation). Must hold raft_mutex.
    void persistLogFull(){ rewriteLog(log_entries); }

    // ------------------------------------------------------------
    // Step down to FOLLOWER on a higher term. Must hold raft_mutex.
    // Persists (fsync) before returning. Clears known leader.
    // ------------------------------------------------------------
    void stepDown(uint64_t newTerm){
        role = FOLLOWER;
        metadata.currentTerm = newTerm;
        metadata.votedFor = 0;
        current_leader_id = 0;
        updateMetaData(metadata);
    }

    // ------------------------------------------------------------
    // Apply committed-but-unapplied entries to the state machine.
    // Must hold raft_mutex. Persists commitIndex/lastApplied.
    // ------------------------------------------------------------
    void applyCommitted(){
        bool changed = false;
        while(metadata.lastApplied < metadata.commitIndex){
            uint64_t idx = metadata.lastApplied + 1;
            if(idx > log_entries.size()) break;          // safety
            state_machine.apply(log_entries[idx - 1].command);
            metadata.lastApplied = idx;
            changed = true;
        }
        if(changed) updateMetaData(metadata);
    }

    // ------------------------------------------------------------
    // Commitment rule (§9.3). Must hold raft_mutex and be LEADER.
    // Find the highest N replicated on a majority whose entry is in
    // the CURRENT term, advance commitIndex to it, apply, notify.
    // ------------------------------------------------------------
    void advanceCommit(){
        if(role != LEADER) return;
        uint64_t last = getLastLogIndex();
        for(uint64_t N = last; N > metadata.commitIndex; --N){
            if(termAt(N) != metadata.currentTerm) continue;  // current-term restriction
            size_t count = 1;                                 // leader counts itself
            for(auto& p : peers) if(matchIndex[p.id] >= N) count++;
            if(count >= majority()){
                metadata.commitIndex = N;
                applyCommitted();
                commit_cv.notify_all();
                return;
            }
        }
    }

    // ------------------------------------------------------------
    // Become LEADER. Must hold raft_mutex. Initializes per-peer state,
    // appends a NOOP in the new term (§7.4), kicks replication.
    // ------------------------------------------------------------
    void becomeLeader(){
        role = LEADER;
        current_leader_id = node_id;

        // §7.4 NOOP: commit something in our own term so earlier entries
        // become committable and waiting clients unblock.
        LogEntry noop;
        noop.term = metadata.currentTerm;
        noop.index = getLastLogIndex() + 1;
        noop.command.operation = OP_NOOP;
        log_entries.push_back(noop);
        writelog(noop);   // append-only, fsync

        uint64_t last = getLastLogIndex();
        for(auto& p : peers){
            nextIndex[p.id]  = last + 1;
            matchIndex[p.id] = 0;
            peerUp[p.id]     = false;
        }

        cout << "[node " << node_id << "] became LEADER for term "
             << metadata.currentTerm << " with " << votes_received
             << " votes" << endl;

        advanceCommit();          // single-node clusters commit immediately
        replicate_cv.notify_all();
    }

    // ------------------------------------------------------------
    int connectToPeer(const string& host, int pport){
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0) return -1;

        struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
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
    // RequestVote handler (§7.1). Acquires raft_mutex.
    // ============================================================
    RequestVoteReply handleRequestVote(const RequestVoteArgs& args){
        std::lock_guard<std::mutex> lock(raft_mutex);

        RequestVoteReply reply;
        reply.term = metadata.currentTerm;
        reply.voteGranted = false;

        if(args.term < metadata.currentTerm) return reply;     // step 1

        if(args.term > metadata.currentTerm) stepDown(args.term); // step 2
        reply.term = metadata.currentTerm;

        bool canVote = (metadata.votedFor == 0 ||
                        metadata.votedFor == args.candidateId);

        // election restriction (§5.4.1)
        uint64_t ourTerm  = getLastLogTerm();
        uint64_t ourIndex = getLastLogIndex();
        bool logOk = (args.lastLogTerm > ourTerm) ||
                     (args.lastLogTerm == ourTerm && args.lastLogIndex >= ourIndex);

        if(canVote && logOk){
            reply.voteGranted = true;
            metadata.votedFor = args.candidateId;
            updateMetaData(metadata);     // persist BEFORE replying
            resetElectionTimer();
            cout << "[node " << node_id << "] voted for node "
                 << args.candidateId << " in term "
                 << metadata.currentTerm << endl;
        }
        return reply;
    }

    // ============================================================
    // AppendEntries handler — full six steps from §7.2.
    // Acquires raft_mutex.
    // ============================================================
    AppendEntriesReply handleAppendEntries(const AppendEntriesArgs& args){
        std::lock_guard<std::mutex> lock(raft_mutex);

        AppendEntriesReply reply;
        reply.term = metadata.currentTerm;
        reply.success = false;

        // Step 1: reply false if leader's term < ours
        if(args.term < metadata.currentTerm) return reply;

        // Step 2: term >= ours -> recognize the leader for this term
        if(args.term > metadata.currentTerm){
            metadata.currentTerm = args.term;
            metadata.votedFor = 0;
            updateMetaData(metadata);
        }
        role = FOLLOWER;
        current_leader_id = args.leaderId;
        resetElectionTimer();
        reply.term = metadata.currentTerm;

        // Step 3: log-matching check
        if(args.prevLogIndex > 0){
            if(args.prevLogIndex > log_entries.size()) return reply;       // too short
            if(termAt(args.prevLogIndex) != args.prevLogTerm) return reply; // term mismatch
        }

        // Steps 4 & 5: reconcile entries with our log
        bool logChanged = false;
        for(const auto& entry : args.entries){
            uint64_t idx = entry.index;
            if(idx <= log_entries.size()){
                if(log_entries[idx - 1].term != entry.term){
                    // Step 4: conflict -> delete this entry and all that follow
                    log_entries.resize(idx - 1);
                    log_entries.push_back(entry);
                    logChanged = true;
                }
                // else: already present and matching -> skip (idempotent)
            }else{
                // Step 5: brand new entry
                log_entries.push_back(entry);
                logChanged = true;
            }
        }
        if(logChanged) persistLogFull();

        // Step 6: advance commit from leader
        if(args.leaderCommit > metadata.commitIndex){
            uint64_t lastNew = args.prevLogIndex + args.entries.size();
            uint64_t newCommit = std::min(args.leaderCommit, lastNew);
            if(newCommit > getLastLogIndex()) newCommit = getLastLogIndex();
            if(newCommit > metadata.commitIndex){
                metadata.commitIndex = newCommit;
                applyCommitted();
                commit_cv.notify_all();
            }
        }

        reply.success = true;   // step 7
        return reply;
    }

    // ============================================================
    // One incoming peer connection (one-shot RPC). Detached thread.
    // ============================================================
    void handlePeerConnection(int fd){
        struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        RpcType type;
        vector<uint8_t> body;
        if(!recvRpc(fd, type, body)){ close(fd); return; }

        if(type == RPC_REQUEST_VOTE_ARGS){
            auto args  = deserializeRequestVoteArgs(body.data());
            auto reply = handleRequestVote(args);
            auto rbody = serializeRequestVoteReply(reply);
            sendRpc(fd, RPC_REQUEST_VOTE_REPLY, rbody);
        }else if(type == RPC_APPEND_ENTRIES_ARGS){
            auto args  = deserializeAppendEntriesArgs(body.data(), (uint32_t)body.size());
            auto reply = handleAppendEntries(args);
            auto rbody = serializeAppendEntriesReply(reply);
            sendRpc(fd, RPC_APPEND_ENTRIES_REPLY, rbody);
        }
        close(fd);
    }

    // ============================================================
    // Broadcast RequestVote to all peers. Detached threads.
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
                int fd = connectToPeer(peer.host, peer.port);
                if(fd < 0) return;

                auto body = serializeRequestVoteArgs(args);
                if(!sendRpc(fd, RPC_REQUEST_VOTE_ARGS, body)){ close(fd); return; }

                RpcType rt; vector<uint8_t> rb;
                if(!recvRpc(fd, rt, rb)){ close(fd); return; }
                close(fd);

                if(rt != RPC_REQUEST_VOTE_REPLY || rb.size() < 9) return;
                auto reply = deserializeRequestVoteReply(rb.data());

                std::lock_guard<std::mutex> lk(raft_mutex);
                if(reply.term > metadata.currentTerm){ stepDown(reply.term); return; }
                if(role != CANDIDATE || metadata.currentTerm != electionTerm) return;

                if(reply.voteGranted){
                    votes_received++;
                    if(votes_received >= majority()) becomeLeader();
                }
            }).detach();
        }
    }

    // ============================================================
    // Per-peer replication thread. One per peer. Sends AppendEntries
    // (heartbeat or real entries) at least every 100 ms, immediately
    // when kicked, and backtracks fast on log mismatch.
    // ============================================================
    void replicationLoop(Peer peer){
        while(running){
            {
                std::unique_lock<std::mutex> lock(raft_mutex);
                replicate_cv.wait_for(lock, std::chrono::milliseconds(100));
                if(!running) break;
                if(role != LEADER) continue;
            }

            // Drive this peer forward until it is caught up, it errors,
            // or we are no longer leader. Backtracking retries happen here
            // with no inter-retry delay so a far-behind follower catches up fast.
            bool keepGoing = true;
            while(keepGoing && running){
                AppendEntriesArgs args;
                uint64_t electionTerm, sentUpTo, prevIndex;
                {
                    std::unique_lock<std::mutex> lock(raft_mutex);
                    if(role != LEADER){ break; }
                    electionTerm   = metadata.currentTerm;
                    uint64_t ni    = nextIndex[peer.id];
                    if(ni < 1) ni = 1;
                    prevIndex      = ni - 1;
                    args.term         = electionTerm;
                    args.leaderId     = node_id;
                    args.prevLogIndex = prevIndex;
                    args.prevLogTerm  = termAt(prevIndex);
                    args.leaderCommit = metadata.commitIndex;
                    for(uint64_t i = ni; i <= log_entries.size(); ++i)
                        args.entries.push_back(log_entries[i - 1]);
                    sentUpTo = log_entries.size();
                }

                int fd = connectToPeer(peer.host, peer.port);
                if(fd < 0){
                    std::lock_guard<std::mutex> lk(raft_mutex);
                    peerUp[peer.id] = false;
                    break;   // peer unreachable; wait for next tick
                }

                auto body = serializeAppendEntriesArgs(args);
                bool netOk = sendRpc(fd, RPC_APPEND_ENTRIES_ARGS, body);
                RpcType rt; vector<uint8_t> rb;
                if(netOk) netOk = recvRpc(fd, rt, rb);
                close(fd);

                if(!netOk || rt != RPC_APPEND_ENTRIES_REPLY || rb.size() < 9){
                    std::lock_guard<std::mutex> lk(raft_mutex);
                    peerUp[peer.id] = false;
                    break;
                }
                auto reply = deserializeAppendEntriesReply(rb.data());

                std::lock_guard<std::mutex> lk(raft_mutex);
                peerUp[peer.id] = true;
                if(reply.term > metadata.currentTerm){ stepDown(reply.term); break; }
                if(role != LEADER || metadata.currentTerm != electionTerm) break;

                if(reply.success){
                    uint64_t newMatch = prevIndex + args.entries.size();
                    if(newMatch > matchIndex[peer.id]) matchIndex[peer.id] = newMatch;
                    nextIndex[peer.id] = matchIndex[peer.id] + 1;
                    advanceCommit();
                    // caught up to what we knew about? stop until next kick/tick.
                    if(nextIndex[peer.id] > sentUpTo) keepGoing = false;
                }else{
                    // log mismatch: back up one and retry immediately (fast path)
                    if(nextIndex[peer.id] > 1) nextIndex[peer.id]--;
                    else keepGoing = false;
                }
            }
        }
    }

    // ============================================================
    // Background: accept peer RPC connections on peer_port.
    // ============================================================
    void rpcReceiverLoop(){
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){
            cerr << "[node " << node_id << "] rpcReceiver: socket failed" << endl;
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
            cerr << "[node " << node_id << "] rpcReceiver: bind failed on port "
                 << peer_port << endl;
            close(listen_fd); return;
        }
        if(listen(listen_fd, 16) < 0){
            cerr << "[node " << node_id << "] rpcReceiver: listen failed" << endl;
            close(listen_fd); return;
        }
        cout << "[node " << node_id << "] listening for peer RPCs on port "
             << peer_port << endl;

        while(running){
            struct pollfd pfd; pfd.fd = listen_fd; pfd.events = POLLIN; pfd.revents = 0;
            int ret = poll(&pfd, 1, 200);
            if(ret <= 0) continue;
            struct sockaddr_in pa; socklen_t pl = sizeof(pa);
            int cfd = accept(listen_fd, (struct sockaddr*)&pa, &pl);
            if(cfd < 0) continue;
            std::thread(&RaftServer::handlePeerConnection, this, cfd).detach();
        }
        close(listen_fd);
    }

    // ============================================================
    // Background: election timer. On timeout -> CANDIDATE.
    // ============================================================
    void electionTimerLoop(){
        while(running){
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            std::unique_lock<std::mutex> lock(raft_mutex);
            if(!running) break;
            if(role == LEADER) continue;
            if(std::chrono::steady_clock::now() < election_deadline) continue;

            role = CANDIDATE;
            metadata.currentTerm++;
            metadata.votedFor = node_id;
            current_leader_id = 0;
            votes_received = 1;
            updateMetaData(metadata);
            cout << "[node " << node_id << "] election timeout -> CANDIDATE term "
                 << metadata.currentTerm << endl;
            resetElectionTimer();

            if(majority() <= 1){            // single-node cluster: win instantly
                becomeLeader();
                continue;
            }
            lock.unlock();
            sendRequestVoteToAll();
        }
    }

    // ============================================================
    // Client command dispatch. Caller holds `lock` (unique_lock on
    // raft_mutex). Returns with the lock still held.
    // ============================================================
    string notLeaderMsg(){
        if(current_leader_id == 0) return "UNKNOWN_LEADER\n";
        return "NOT_LEADER leader=" + to_string(current_leader_id) + "\n";
    }

    // Append a command and wait for it to commit. Caller holds lock.
    string proposeAndWait(std::unique_lock<std::mutex>& lock,
                          const Command& cmd, bool isRead, const string& readKey){
        if(role != LEADER) return notLeaderMsg();

        LogEntry e;
        e.term  = metadata.currentTerm;
        e.index = getLastLogIndex() + 1;
        e.command = cmd;
        log_entries.push_back(e);
        writelog(e);                 // append-only, fsync
        uint64_t idx  = e.index;
        uint64_t term = e.term;

        advanceCommit();             // single-node commits right away
        replicate_cv.notify_all();   // kick senders for the multi-node case

        bool ok = commit_cv.wait_for(lock, std::chrono::seconds(3), [&]{
            return !running || role != LEADER || metadata.commitIndex >= idx;
        });
        (void)ok;

        if(!running) return "ERR: shutting down\n";
        if(metadata.commitIndex < idx || role != LEADER){
            // We lost leadership (or timed out) before this entry committed.
            if(role != LEADER) return notLeaderMsg();
            return "ERR: commit timeout (no majority?)\n";
        }

        if(isRead){
            if(state_machine.has(readKey)) return state_machine.get(readKey) + "\n";
            return "NOT_FOUND\n";
        }
        return "OK (committed at index " + to_string(idx) +
               " in term " + to_string(term) + ")\n";
    }

    string handlePut(std::unique_lock<std::mutex>& lock,
                     const string& key, const string& value){
        Command c;
        c.operation = OP_PUT;
        c.key = key; c.value = value;
        c.key_len = (u_int16_t)key.size();
        c.value_len = (u_int16_t)value.size();
        return proposeAndWait(lock, c, false, "");
    }

    string handleDelete(std::unique_lock<std::mutex>& lock, const string& key){
        Command c;
        c.operation = OP_DELETE;
        c.key = key; c.key_len = (u_int16_t)key.size();
        c.value_len = 0;
        return proposeAndWait(lock, c, false, "");
    }

    // Linearizable read: append a NOOP read-marker, wait for it to commit,
    // then read the value (§9.4 log-based reads).
    string handleGet(std::unique_lock<std::mutex>& lock, const string& key){
        Command c; c.operation = OP_NOOP;
        return proposeAndWait(lock, c, true, key);
    }

    // Local, non-replicated read of THIS node's applied state machine.
    // For testing only (chaos test compares per-node state). Never redirects.
    string handleDump(){
        const auto& all = state_machine.getAll();
        vector<pair<string,string>> kv(all.begin(), all.end());
        std::sort(kv.begin(), kv.end());
        string out;
        for(auto& p : kv) out += p.first + "=" + p.second + "\n";
        out += "__count__=" + to_string(kv.size()) + "\n";
        return out;
    }

    // Single-line digest of THIS node's applied state machine: a key count plus
    // an order-independent CRC32 over the sorted key=value set. Lets the chaos
    // test compare per-node state with one tiny response (no huge transfers).
    // Never redirects — it reports the local state machine.
    string handleHash(){
        const auto& all = state_machine.getAll();
        vector<pair<string,string>> kv(all.begin(), all.end());
        std::sort(kv.begin(), kv.end());
        string blob;
        for(auto& p : kv){ blob += p.first; blob += '='; blob += p.second; blob += '\n'; }
        vector<u_int8_t> bytes(blob.begin(), blob.end());
        u_int32_t h = computeCRC32(bytes);
        return "count=" + to_string(kv.size()) + " hash=" + to_string(h) + "\n";
    }

    string handleStatus(){
        string r;
        r += "node_id:      " + to_string(node_id) + "\n";
        r += "state:        " + roleToString(role) + "\n";
        r += "term:         " + to_string(metadata.currentTerm) + "\n";
        r += "leader:       " + (current_leader_id ? to_string(current_leader_id)
                                                    : string("unknown")) + "\n";
        r += "commitIndex:  " + to_string(metadata.commitIndex) + "\n";
        r += "lastApplied:  " + to_string(metadata.lastApplied) + "\n";
        r += "log length:   " + to_string(log_entries.size()) + "\n";
        if(role == LEADER){
            r += "peers:\n";
            for(auto& p : peers){
                r += "  node " + to_string(p.id) +
                     ": matchIndex=" + to_string(matchIndex[p.id]) +
                     ", nextIndex=" + to_string(nextIndex[p.id]) +
                     ", " + (peerUp[p.id] ? "up" : "down") + "\n";
            }
        }
        return r;
    }

    string handleCommand(std::unique_lock<std::mutex>& lock, const string& line){
        string trimmed = line;
        while(!trimmed.empty() && (trimmed.back()=='\n' || trimmed.back()=='\r'))
            trimmed.pop_back();
        if(trimmed.empty()) return "";

        istringstream iss(trimmed);
        string cmd; iss >> cmd;
        string up = cmd;
        for(auto& c : up) c = toupper(c);

        if(up == "PUT"){
            string k, v; iss >> k >> v;
            if(k.empty()) return "ERR: PUT requires key and value\n";
            if(v.empty()) return "ERR: PUT requires a value\n";
            return handlePut(lock, k, v);
        }else if(up == "GET"){
            string k; iss >> k;
            if(k.empty()) return "ERR: GET requires a key\n";
            return handleGet(lock, k);
        }else if(up == "DELETE"){
            string k; iss >> k;
            if(k.empty()) return "ERR: DELETE requires a key\n";
            return handleDelete(lock, k);
        }else if(trimmed == "\\status"){
            return handleStatus();
        }else if(trimmed == "\\dump"){
            return handleDump();
        }else if(trimmed == "\\hash"){
            return handleHash();
        }else if(up == "QUIT"){
            return "QUIT";
        }
        return "ERR: unknown command '" + cmd + "'\n";
    }

    // Send a full buffer, looping until everything is written. A bare send()
    // can do a short write (the socket buffer fills), silently truncating large
    // responses like \dump — so every client reply must go through here.
    bool sendAllClient(int fd, const string& s){
        const char* p = s.data();
        size_t left = s.size();
        while(left > 0){
            ssize_t n = send(fd, p, left, 0);
            if(n <= 0){
                if(n < 0 && errno == EINTR) continue;
                return false;
            }
            p += n; left -= (size_t)n;
        }
        return true;
    }

    // ============================================================
    // One client connection handled in its own thread, so a PUT that
    // blocks waiting for commit never stalls other clients.
    // ============================================================
    void handleClient(int cfd){
        cout << "[node " << node_id << "] client connected" << endl;
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 300000;  // 300ms
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        string buf; char tmp[4096];
        while(running){
            ssize_t n = recv(cfd, tmp, sizeof(tmp)-1, 0);
            if(n < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }
            if(n == 0) break;
            buf.append(tmp, n);

            size_t nl;
            while((nl = buf.find('\n')) != string::npos){
                string line = buf.substr(0, nl + 1);
                buf.erase(0, nl + 1);

                std::unique_lock<std::mutex> lock(raft_mutex);
                string resp = handleCommand(lock, line);
                lock.unlock();

                if(resp == "QUIT"){
                    sendAllClient(cfd, "Goodbye\n");
                    close(cfd);
                    cout << "[node " << node_id << "] client quit" << endl;
                    return;
                }
                if(!resp.empty()){
                    if(!sendAllClient(cfd, resp)){ close(cfd); return; }
                }
            }
        }
        close(cfd);
        cout << "[node " << node_id << "] client disconnected" << endl;
    }

public:
    RaftServer(u_int64_t id, int p, int pp,
               const vector<std::tuple<uint64_t,string,int>>& peer_list = {})
        : node_id(id), port(p), peer_port(pp),
          role(FOLLOWER), running(false), votes_received(0),
          rng(std::random_device{}() ^ (unsigned)id),
          current_leader_id(0) {
        for(auto& t : peer_list)
            peers.push_back({std::get<0>(t), std::get<1>(t), std::get<2>(t)});
    }

    // Recovery: load persistent state, replay COMMITTED entries only.
    bool recover(){
        if(!initStorage()){
            cerr << "[node " << node_id << "] failed to initialize storage" << endl;
            return false;
        }

        metadata = getMetaData();

        size_t valid_bytes;
        log_entries = readLogs(valid_bytes);

        string log_path = DATA_DIR + "/logs.bin";
        struct stat st;
        if(stat(log_path.c_str(), &st)==0 && (size_t)st.st_size > valid_bytes){
            cerr << "[node " << node_id << "] truncating corrupt log tail ("
                 << st.st_size << " -> " << valid_bytes << " bytes)" << endl;
            truncateLogFile(valid_bytes);
        }

        // commitIndex/lastApplied are volatile. The in-memory state machine is
        // empty on boot, so lastApplied MUST reset to 0 and the state machine is
        // rebuilt by replaying the log up to the durable commitIndex. Trusting a
        // persisted lastApplied here would skip the replay and leave committed
        // keys missing. Only entries up to commitIndex are safe to apply; the
        // uncommitted tail stays on disk but unapplied (a leader may overwrite it).
        if(metadata.commitIndex > log_entries.size())
            metadata.commitIndex = log_entries.size();
        metadata.lastApplied = 0;
        for(uint64_t i = 0; i < metadata.commitIndex; ++i)
            state_machine.apply(log_entries[i].command);
        metadata.lastApplied = metadata.commitIndex;
        updateMetaData(metadata);

        string voted = (metadata.votedFor==0) ? "none" : to_string(metadata.votedFor);
        cout << "[node " << node_id << "] persistent state loaded: currentTerm="
             << metadata.currentTerm << " votedFor=" << voted
             << " log=[" << log_entries.size() << " entries]"
             << " commitIndex=" << metadata.commitIndex << endl;
        return true;
    }

    void run(){
        running = true;
        { std::lock_guard<std::mutex> lock(raft_mutex); resetElectionTimer(); }

        // Background threads
        std::vector<std::thread> threads;
        threads.emplace_back(&RaftServer::rpcReceiverLoop, this);
        threads.emplace_back(&RaftServer::electionTimerLoop, this);
        for(auto& p : peers)
            threads.emplace_back(&RaftServer::replicationLoop, this, p);

        // Client TCP listener
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){
            cerr << "[node " << node_id << "] failed to create client socket" << endl;
            running = false;
            replicate_cv.notify_all();
            for(auto& t : threads) t.join();
            return;
        }
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if(bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            cerr << "[node " << node_id << "] failed to bind client port " << port << endl;
            close(listen_fd); running = false;
            replicate_cv.notify_all();
            for(auto& t : threads) t.join();
            return;
        }
        if(listen(listen_fd, 16) < 0){
            cerr << "[node " << node_id << "] failed to listen on client port" << endl;
            close(listen_fd); running = false;
            replicate_cv.notify_all();
            for(auto& t : threads) t.join();
            return;
        }
        cout << "[node " << node_id << "] ready for client commands on port "
             << port << endl;

        while(running){
            struct pollfd pfd; pfd.fd = listen_fd; pfd.events = POLLIN; pfd.revents = 0;
            int ret = poll(&pfd, 1, 300);
            if(ret <= 0) continue;
            struct sockaddr_in ca; socklen_t cl = sizeof(ca);
            int cfd = accept(listen_fd, (struct sockaddr*)&ca, &cl);
            if(cfd < 0) continue;
            std::thread(&RaftServer::handleClient, this, cfd).detach();
        }

        close(listen_fd);
        replicate_cv.notify_all();
        commit_cv.notify_all();
        for(auto& t : threads) t.join();
    }
};

#endif // SERVER_CPP
