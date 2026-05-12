#include <bits/stdc++.h>
#include "log.cpp"
#include "command.cpp"
#include "server.cpp"

using namespace std;
// Vote requresting varaibles for raft consensus protocol
struct RequestVoteArgs{
    u_int64_t term;
    u_int64_t candidateId;
    u_int64_t lastLogIndex;
    u_int64_t lastLogTerm;
};

// Vote requresting reply variables for raft consensus protocol
struct RequestVoteReply{
    u_int64_t term;
    bool voteGranted;
};

// Append entries varaibles for raft consensus protocol
struct AppendEntriesArgs{
    u_int64_t term;
    u_int64_t leaderId;
    u_int64_t prevLogIndex;
    u_int64_t prevLogTerm;
    vector<LogEntry> entries;
    u_int64_t leaderCommit;
};

// Append entries reply variables for raft consensus protocol
struct AppendEntriesReply{
    u_int64_t term;
    bool success;
};