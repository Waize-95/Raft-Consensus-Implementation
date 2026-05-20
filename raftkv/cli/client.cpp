#include<iostream>
#include<string>
#include<cstring>
#include<vector>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<unistd.h>
#include<poll.h>
#include<thread>
#include<chrono>
using namespace std;

// A leader-aware TCP client. It is configured with the addresses of all
// cluster nodes (id@host:clientport). On NOT_LEADER / UNKNOWN_LEADER / timeout
// it transparently reconnects to the right node and retries the command, so
// callers (interactive or piped) just send a command and get the final answer.
class RaftClient{
private:
    struct Server { uint64_t id; string host; int port; };
    vector<Server> servers;
    int current;          // index into servers of the active target
    int sock_fd;
    bool connected;

    void closeConn(){
        if(sock_fd >= 0){ close(sock_fd); sock_fd = -1; }
        connected = false;
    }

    bool dial(const string& host, int port){
        closeConn();
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(sock_fd < 0) return false;

        struct hostent* he = gethostbyname(host.c_str());
        if(!he){ closeConn(); return false; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        addr.sin_port = htons(port);

        if(connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            closeConn(); return false;
        }
        connected = true;
        return true;
    }

    // Find the server-list index for a node id, or -1.
    int indexForId(uint64_t id){
        for(size_t i=0;i<servers.size();i++) if(servers[i].id == id) return (int)i;
        return -1;
    }

    // Send one command on the current connection and read one full response.
    // Returns false on connection / timeout failure.
    //
    // For single-line commands we stop at the first trailing newline (fast).
    // For multi-line commands (\status, \dump) a single line ends in '\n' too,
    // so instead we read until the stream goes idle for a short window — this
    // safely reassembles arbitrarily large multi-line replies.
    bool sendOnce(const string& cmd, string& response, bool multiline){
        if(!connected) return false;
        string msg = cmd + "\n";
        if(send(sock_fd, msg.c_str(), msg.size(), 0) <= 0){ closeConn(); return false; }

        struct pollfd pfd; pfd.fd = sock_fd; pfd.events = POLLIN; pfd.revents = 0;
        response.clear();
        bool got = false;
        while(true){
            int ret = poll(&pfd, 1, got ? (multiline ? 200 : 5000) : 5000);
            if(ret <= 0){
                if(got && multiline) return true;   // idle after data => complete
                return false;                        // timed out with no/partial data
            }
            char buf[4096];
            ssize_t n = recv(sock_fd, buf, sizeof(buf)-1, 0);
            if(n <= 0){ closeConn(); return false; }
            response.append(buf, n);
            got = true;
            if(!multiline && !response.empty() && response.back() == '\n') break;
        }
        return true;
    }

public:
    RaftClient():current(0), sock_fd(-1), connected(false){}
    ~RaftClient(){ closeConn(); }

    void addServer(uint64_t id, const string& host, int port){
        servers.push_back({id, host, port});
    }
    size_t serverCount() const { return servers.size(); }

    bool ensureConnected(){
        if(connected) return true;
        for(size_t tries=0; tries<servers.size(); ++tries){
            Server& s = servers[current];
            if(dial(s.host, s.port)) return true;
            current = (current + 1) % servers.size();
        }
        return false;
    }

    string describeCurrent(){
        Server& s = servers[current];
        return s.host + ":" + to_string(s.port);
    }

    // Execute a command, following leader redirects. Backslash commands
    // (\status, \dump) are local queries: sent to the current node only.
    string execute(const string& line){
        bool isLocal = (!line.empty() && line[0] == '\\');
        const int MAX_TRIES = 20;

        for(int attempt=0; attempt<MAX_TRIES; ++attempt){
            if(!ensureConnected()) return "ERR: no server reachable\n";

            string resp;
            if(!sendOnce(line, resp, isLocal)){
                // connection/timeout problem: rotate and retry
                closeConn();
                current = (current + 1) % servers.size();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                continue;
            }

            if(isLocal) return resp;   // never redirect local queries

            if(resp.rfind("NOT_LEADER", 0) == 0){
                // parse "leader=N"
                size_t eq = resp.find('=');
                int target = -1;
                if(eq != string::npos)
                    target = indexForId(strtoull(resp.c_str()+eq+1, nullptr, 10));
                closeConn();
                if(target >= 0) current = target;
                else current = (current + 1) % servers.size();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if(resp.rfind("UNKNOWN_LEADER", 0) == 0 ||
               resp.rfind("ERR: commit timeout", 0) == 0){
                closeConn();
                current = (current + 1) % servers.size();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            return resp;   // real answer (OK / value / NOT_FOUND / other ERR)
        }
        return "ERR: gave up after retries (no stable leader?)\n";
    }

    void quit(){
        if(connected){
            string resp;
            sendOnce("QUIT", resp, false);
        }
        closeConn();
    }
};
