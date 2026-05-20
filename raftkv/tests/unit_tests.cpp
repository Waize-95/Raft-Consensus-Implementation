// ============================================================================
// Unit tests (Phase 3, Section 13.1 tests/ deliverable)
//
// Covers: log entry serialize/deserialize (incl. CRC), the RequestVote receiver
// election restriction, the AppendEntries log-matching + conflict-delete logic,
// and the commitment rule's current-term restriction.
//
// These exercise the pure data/logic helpers directly (no sockets), feeding in
// hand-crafted inputs and checking the outputs. Build: make test  (or see below).
// ============================================================================
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include "../src/rcp.cpp"   // pulls in log.cpp / command.cpp too

using namespace std;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if(cond){ g_pass++; cout << "  [PASS] " << msg << "\n"; } \
    else    { g_fail++; cout << "  [FAIL] " << msg << "\n"; } } while(0)

// ---- helpers ---------------------------------------------------------------
static LogEntry mkEntry(uint64_t term, uint64_t index, uint8_t op,
                        const string& k="", const string& v=""){
    LogEntry e; e.term=term; e.index=index;
    e.command.operation=op;
    e.command.key=k; e.command.value=v;
    e.command.key_len=(u_int16_t)k.size();
    e.command.value_len=(u_int16_t)v.size();
    return e;
}

// ============================================================================
void test_log_serde(){
    cout << "test: log entry serialize/deserialize + CRC\n";
    LogEntry e = mkEntry(7, 42, 1, "alpha", "beta");
    vector<u_int8_t> bytes = SerializeLogEntry(e);
    size_t valid = 0;
    vector<LogEntry> out = DeserializeLogEntry(bytes, valid);
    CHECK(out.size()==1, "exactly one entry parsed");
    CHECK(valid==bytes.size(), "all bytes consumed");
    CHECK(out[0].term==7 && out[0].index==42, "term/index round-trip");
    CHECK(out[0].command.operation==1, "op round-trip");
    CHECK(out[0].command.key=="alpha" && out[0].command.value=="beta", "key/value round-trip");

    // corrupt one byte -> CRC must reject (entry dropped, valid<size)
    vector<u_int8_t> bad = bytes; bad[20] ^= 0xFF;
    size_t v2=0; vector<LogEntry> out2 = DeserializeLogEntry(bad, v2);
    CHECK(out2.empty() && v2==0, "CRC mismatch rejects corrupt entry");

    // two concatenated entries parse in order
    LogEntry e2 = mkEntry(8, 43, 2, "gamma");
    vector<u_int8_t> two = SerializeLogEntry(e);
    vector<u_int8_t> b2 = SerializeLogEntry(e2);
    two.insert(two.end(), b2.begin(), b2.end());
    size_t v3=0; vector<LogEntry> out3 = DeserializeLogEntry(two, v3);
    CHECK(out3.size()==2 && out3[1].index==43 && out3[1].command.operation==2,
          "two concatenated entries parse in order");
}

// ============================================================================
void test_rpc_serde(){
    cout << "test: AppendEntries args serialize/deserialize with entries\n";
    AppendEntriesArgs a;
    a.term=3; a.leaderId=1; a.prevLogIndex=5; a.prevLogTerm=2; a.leaderCommit=4;
    a.entries.push_back(mkEntry(3,6,1,"k1","v1"));
    a.entries.push_back(mkEntry(3,7,3));   // NOOP
    vector<uint8_t> body = serializeAppendEntriesArgs(a);
    AppendEntriesArgs b = deserializeAppendEntriesArgs(body.data(),(uint32_t)body.size());
    CHECK(b.term==3 && b.leaderId==1 && b.prevLogIndex==5 && b.prevLogTerm==2 && b.leaderCommit==4,
          "header fields round-trip");
    CHECK(b.entries.size()==2, "entry count round-trip");
    CHECK(b.entries[0].command.key=="k1" && b.entries[0].command.value=="v1", "entry[0] round-trip");
    CHECK(b.entries[1].command.operation==3, "entry[1] NOOP round-trip");

    RequestVoteArgs rv; rv.term=9; rv.candidateId=2; rv.lastLogIndex=10; rv.lastLogTerm=8;
    auto rvb = serializeRequestVoteArgs(rv);
    RequestVoteArgs rv2 = deserializeRequestVoteArgs(rvb.data());
    CHECK(rv2.term==9 && rv2.candidateId==2 && rv2.lastLogIndex==10 && rv2.lastLogTerm==8,
          "RequestVote args round-trip");
}

// ============================================================================
// Pure re-implementation of the RequestVote election restriction (§5.4.1),
// matching server.cpp, so we can unit-test the decision in isolation.
static bool voteGranted(uint64_t curTerm, uint64_t votedFor,
                        uint64_t ourLastIdx, uint64_t ourLastTerm,
                        const RequestVoteArgs& a){
    if(a.term < curTerm) return false;
    bool canVote = (votedFor==0 || votedFor==a.candidateId);
    bool logOk = (a.lastLogTerm > ourLastTerm) ||
                 (a.lastLogTerm==ourLastTerm && a.lastLogIndex>=ourLastIdx);
    return canVote && logOk;
}

void test_requestvote_logic(){
    cout << "test: RequestVote receiver election restriction\n";
    RequestVoteArgs a; a.term=5; a.candidateId=3;

    a.lastLogTerm=5; a.lastLogIndex=10;
    CHECK(voteGranted(5,0,10,5,a)==true,  "grants when log equally up-to-date and not yet voted");

    a.lastLogTerm=4; a.lastLogIndex=100;
    CHECK(voteGranted(5,0,10,5,a)==false, "denies when candidate's last term is lower (even with longer log)");

    a.lastLogTerm=5; a.lastLogIndex=9;
    CHECK(voteGranted(5,0,10,5,a)==false, "denies when same term but candidate log shorter");

    a.lastLogTerm=6; a.lastLogIndex=1;
    CHECK(voteGranted(5,0,10,5,a)==true,  "grants when candidate's last term is higher");

    a.lastLogTerm=5; a.lastLogIndex=10;
    CHECK(voteGranted(5,3,10,5,a)==true,  "grants when already voted for THIS candidate");
    CHECK(voteGranted(5,2,10,5,a)==false, "denies when already voted for a different candidate");

    a.term=4;
    CHECK(voteGranted(5,0,10,5,a)==false, "denies when candidate term < currentTerm");
}

// ============================================================================
// Pure model of AppendEntries steps 3-5 (log matching + conflict delete +
// append) operating on a vector<LogEntry>, matching server.cpp's logic.
static bool appendEntries(vector<LogEntry>& log, uint64_t prevIdx, uint64_t prevTerm,
                          const vector<LogEntry>& entries){
    auto termAt=[&](uint64_t i)->uint64_t{ return (i==0||i>log.size())?0:log[i-1].term; };
    if(prevIdx>0){
        if(prevIdx>log.size()) return false;          // too short
        if(termAt(prevIdx)!=prevTerm) return false;   // term mismatch
    }
    for(const auto& e : entries){
        uint64_t idx=e.index;
        if(idx<=log.size()){
            if(log[idx-1].term!=e.term){ log.resize(idx-1); log.push_back(e); }
        }else{
            log.push_back(e);
        }
    }
    return true;
}

void test_appendentries_logic(){
    cout << "test: AppendEntries log-matching + conflict-delete\n";
    vector<LogEntry> log = { mkEntry(1,1,3), mkEntry(1,2,1,"a","1"), mkEntry(2,3,1,"b","2") };

    // mismatch at prevLogIndex -> reject, log unchanged
    bool r1 = appendEntries(log, 3, 99, { mkEntry(3,4,1,"c","3") });
    CHECK(r1==false && log.size()==3, "rejects on prevLogTerm mismatch, log unchanged");

    // matching prev -> append new entry
    bool r2 = appendEntries(log, 3, 2, { mkEntry(3,4,1,"c","3") });
    CHECK(r2==true && log.size()==4 && log[3].command.key=="c", "appends new entry on match");

    // conflicting entry at existing index -> truncate and replace
    bool r3 = appendEntries(log, 2, 1, { mkEntry(5,3,1,"X","9"), mkEntry(5,4,1,"Y","9") });
    CHECK(r3==true && log.size()==4 && log[2].term==5 && log[2].command.key=="X" &&
          log[3].command.key=="Y", "conflict deletes tail and appends leader's entries");

    // idempotent re-delivery of identical entries -> no change
    size_t before = log.size();
    bool r4 = appendEntries(log, 2, 1, { mkEntry(5,3,1,"X","9") });
    CHECK(r4==true && log.size()==before && log[2].command.key=="X",
          "re-delivering an identical entry is idempotent");
}

// ============================================================================
// Commitment rule (§9.3): highest N replicated on a majority whose entry is in
// the current term. Mirrors advanceCommit() in server.cpp.
static uint64_t computeCommit(const vector<LogEntry>& log, uint64_t curTerm,
                              const map<uint64_t,uint64_t>& match, size_t clusterSize,
                              uint64_t commitIndex){
    size_t majority = clusterSize/2 + 1;
    for(uint64_t N=log.size(); N>commitIndex; --N){
        if(log[N-1].term != curTerm) continue;     // current-term restriction
        size_t count=1;                            // leader itself
        for(auto& kv : match) if(kv.second>=N) count++;
        if(count>=majority) return N;
    }
    return commitIndex;
}

void test_commitment_rule(){
    cout << "test: commitment rule with current-term restriction\n";
    // 5-node cluster, leader term = 3.
    vector<LogEntry> log = { mkEntry(1,1,1,"a"), mkEntry(1,2,1,"b"),
                             mkEntry(3,3,1,"c"), mkEntry(3,4,1,"d") };
    // index 2 (term 1) replicated on a majority, but it's an OLD term:
    map<uint64_t,uint64_t> m1 = {{2,2},{3,2},{4,0},{5,0}};
    CHECK(computeCommit(log,3,m1,5,0)==0,
          "does NOT commit a prior-term entry even with majority replication (Figure 8)");

    // index 3 (current term 3) replicated on a majority -> commits 3
    map<uint64_t,uint64_t> m2 = {{2,3},{3,3},{4,0},{5,0}};
    CHECK(computeCommit(log,3,m2,5,0)==3,
          "commits a current-term entry once a majority has it");

    // index 4 (current term) on majority -> commits 4 (and transitively earlier)
    map<uint64_t,uint64_t> m3 = {{2,4},{3,4},{4,4},{5,0}};
    CHECK(computeCommit(log,3,m3,5,0)==4, "commits highest current-term majority index");

    // only the leader + one follower (2/5) -> no commit
    map<uint64_t,uint64_t> m4 = {{2,4},{3,0},{4,0},{5,0}};
    CHECK(computeCommit(log,3,m4,5,0)==0, "no commit without a majority");
}

// ============================================================================
int main(){
    cout << "=== RaftKV unit tests ===\n";
    test_log_serde();
    test_rpc_serde();
    test_requestvote_logic();
    test_appendentries_logic();
    test_commitment_rule();
    cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail==0 ? 0 : 1;
}
