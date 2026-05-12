// ============================================================
// RPC Layer — Binary message protocol for Raft peer communication
// Phase 2, Step 3
//
// Wire format for every RPC:
//   [Header: 5 bytes]  [Body: variable]
//
// Header layout (big-endian):
//   uint8_t  type    — RpcType enum
//   uint32_t length  — byte length of the body that follows
// ============================================================

#ifndef RCP_CPP
#define RCP_CPP

#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
#include <unistd.h>
#include <errno.h>

// Forward declaration: LogEntry is defined in log.cpp (included earlier in the chain)

// ---- RPC type identifiers ----
enum RpcType : uint8_t {
    RPC_REQUEST_VOTE_ARGS    = 1,
    RPC_REQUEST_VOTE_REPLY   = 2,
    RPC_APPEND_ENTRIES_ARGS  = 3,
    RPC_APPEND_ENTRIES_REPLY = 4
};

// ---- RPC header (5 bytes on the wire) ----
struct RpcHeader {
    uint8_t  type;    // RpcType
    uint32_t length;  // byte length of the body
};

static const size_t RPC_HEADER_SIZE = 5;

// ============================================================
// RequestVote RPC
// ============================================================

// Arguments sent by a candidate requesting votes
struct RequestVoteArgs {
    uint64_t term;
    uint64_t candidateId;
    uint64_t lastLogIndex;
    uint64_t lastLogTerm;
};

// Reply returned to the candidate
struct RequestVoteReply {
    uint64_t term;
    bool     voteGranted;
};

// ============================================================
// AppendEntries RPC (used for heartbeats in Phase 2)
// ============================================================

// Arguments sent by the leader
struct AppendEntriesArgs {
    uint64_t term;
    uint64_t leaderId;
    uint64_t prevLogIndex;
    uint64_t prevLogTerm;
    std::vector<LogEntry> entries;   // empty for heartbeats
    uint64_t leaderCommit;
};

// Reply returned to the leader
struct AppendEntriesReply {
    uint64_t term;
    bool     success;
};

// ============================================================
// Helpers: push / pull fixed-width integers (big-endian)
// ============================================================

static void pushU8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

static void pushU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >>  8) & 0xFF);
    buf.push_back((v      ) & 0xFF);
}

static void pushU64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        buf.push_back((v >> (i * 8)) & 0xFF);
    }
}

static uint8_t pullU8(const uint8_t*& p) {
    return *p++;
}

static uint32_t pullU32(const uint8_t*& p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | *p++;
    return v;
}

static uint64_t pullU64(const uint8_t*& p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | *p++;
    return v;
}

// ============================================================
// Header serialization / deserialization
// ============================================================

std::vector<uint8_t> serializeRpcHeader(RpcType type, uint32_t bodyLen) {
    std::vector<uint8_t> buf;
    buf.reserve(RPC_HEADER_SIZE);
    pushU8(buf, static_cast<uint8_t>(type));
    pushU32(buf, bodyLen);
    return buf;
}

RpcHeader deserializeRpcHeader(const uint8_t* data) {
    RpcHeader h;
    const uint8_t* p = data;
    h.type   = pullU8(p);
    h.length = pullU32(p);
    return h;
}

// ============================================================
// RequestVoteArgs  (body = 32 bytes)
// ============================================================

std::vector<uint8_t> serializeRequestVoteArgs(const RequestVoteArgs& args) {
    std::vector<uint8_t> buf;
    buf.reserve(32);
    pushU64(buf, args.term);
    pushU64(buf, args.candidateId);
    pushU64(buf, args.lastLogIndex);
    pushU64(buf, args.lastLogTerm);
    return buf;
}

RequestVoteArgs deserializeRequestVoteArgs(const uint8_t* data) {
    RequestVoteArgs args;
    const uint8_t* p = data;
    args.term         = pullU64(p);
    args.candidateId  = pullU64(p);
    args.lastLogIndex = pullU64(p);
    args.lastLogTerm  = pullU64(p);
    return args;
}

// ============================================================
// RequestVoteReply  (body = 9 bytes)
// ============================================================

std::vector<uint8_t> serializeRequestVoteReply(const RequestVoteReply& reply) {
    std::vector<uint8_t> buf;
    buf.reserve(9);
    pushU64(buf, reply.term);
    pushU8(buf, reply.voteGranted ? 1 : 0);
    return buf;
}

RequestVoteReply deserializeRequestVoteReply(const uint8_t* data) {
    RequestVoteReply reply;
    const uint8_t* p = data;
    reply.term        = pullU64(p);
    reply.voteGranted = (pullU8(p) != 0);
    return reply;
}

// ============================================================
// AppendEntriesArgs  (body = 44 bytes + N × serialized LogEntry)
// ============================================================

std::vector<uint8_t> serializeAppendEntriesArgs(const AppendEntriesArgs& args) {
    std::vector<uint8_t> buf;
    buf.reserve(44);
    pushU64(buf, args.term);
    pushU64(buf, args.leaderId);
    pushU64(buf, args.prevLogIndex);
    pushU64(buf, args.prevLogTerm);
    pushU64(buf, args.leaderCommit);

    // Entry count (uint32_t — up to ~4 billion entries per RPC, plenty)
    uint32_t count = static_cast<uint32_t>(args.entries.size());
    pushU32(buf, count);

    // Serialize each LogEntry using the existing SerializeLogEntry()
    for (const auto& entry : args.entries) {
        std::vector<uint8_t> entryBytes = SerializeLogEntry(entry);
        // Prefix each entry with its byte length so the receiver knows where it ends
        pushU32(buf, static_cast<uint32_t>(entryBytes.size()));
        buf.insert(buf.end(), entryBytes.begin(), entryBytes.end());
    }
    return buf;
}

AppendEntriesArgs deserializeAppendEntriesArgs(const uint8_t* data, uint32_t bodyLen) {
    AppendEntriesArgs args;
    const uint8_t* p   = data;
    const uint8_t* end = data + bodyLen;

    args.term         = pullU64(p);
    args.leaderId     = pullU64(p);
    args.prevLogIndex = pullU64(p);
    args.prevLogTerm  = pullU64(p);
    args.leaderCommit = pullU64(p);

    uint32_t count = pullU32(p);

    for (uint32_t i = 0; i < count && p < end; ++i) {
        uint32_t entryLen = pullU32(p);
        if (p + entryLen > end) break; // safety check

        // DeserializeLogEntry expects the full blob and returns a vector;
        // we feed it exactly entryLen bytes for one entry.
        size_t validBytes = 0;
        std::vector<uint8_t> entryData(p, p + entryLen);
        std::vector<LogEntry> parsed = DeserializeLogEntry(entryData, validBytes);
        if (!parsed.empty()) {
            args.entries.push_back(parsed[0]);
        }
        p += entryLen;
    }
    return args;
}

// ============================================================
// AppendEntriesReply  (body = 9 bytes)
// ============================================================

std::vector<uint8_t> serializeAppendEntriesReply(const AppendEntriesReply& reply) {
    std::vector<uint8_t> buf;
    buf.reserve(9);
    pushU64(buf, reply.term);
    pushU8(buf, reply.success ? 1 : 0);
    return buf;
}

AppendEntriesReply deserializeAppendEntriesReply(const uint8_t* data) {
    AppendEntriesReply reply;
    const uint8_t* p = data;
    reply.term    = pullU64(p);
    reply.success = (pullU8(p) != 0);
    return reply;
}

// ============================================================
// TCP helpers: reliable send / recv over a stream socket
// ============================================================

// Send exactly `len` bytes; returns true on success.
static bool sendAll(int fd, const void* data, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;   // peer closed or error
        }
        ptr       += n;
        remaining -= n;
    }
    return true;
}

// Receive exactly `len` bytes; returns true on success.
static bool recvAll(int fd, void* data, size_t len) {
    uint8_t* ptr = static_cast<uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::recv(fd, ptr, remaining, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;   // peer closed or error
        }
        ptr       += n;
        remaining -= n;
    }
    return true;
}

// ---- High-level send: header + body in one call ----
bool sendRpc(int fd, RpcType type, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> header = serializeRpcHeader(type, static_cast<uint32_t>(body.size()));
    if (!sendAll(fd, header.data(), header.size())) return false;
    if (!body.empty()) {
        if (!sendAll(fd, body.data(), body.size())) return false;
    }
    return true;
}

// ---- High-level recv: reads header, then body ----
// Returns false on disconnect / error.
bool recvRpc(int fd, RpcType& type, std::vector<uint8_t>& body) {
    uint8_t headerBuf[RPC_HEADER_SIZE];
    if (!recvAll(fd, headerBuf, RPC_HEADER_SIZE)) return false;

    RpcHeader h = deserializeRpcHeader(headerBuf);
    type = static_cast<RpcType>(h.type);

    body.resize(h.length);
    if (h.length > 0) {
        if (!recvAll(fd, body.data(), h.length)) return false;
    }
    return true;
}

#endif // RCP_CPP