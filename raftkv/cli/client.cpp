#include<iostream>
#include<string>
#include<cstring>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<unistd.h>
#include<poll.h>
using namespace std;

// Simple TCP client for connecting to a RaftKV server
class RaftClient{
private:
    int sock_fd;
    bool connected;

public:
    RaftClient():sock_fd(-1),connected(false){}

    ~RaftClient(){
        disconnect();
    }

    bool connect_to(const string& host, int port){
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(sock_fd < 0){
            cerr<<"Failed to create socket"<<endl;
            return false;
        }

        struct hostent* he = gethostbyname(host.c_str());
        if(!he){
            cerr<<"Failed to resolve host: "<<host<<endl;
            close(sock_fd);
            sock_fd = -1;
            return false;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        addr.sin_port = htons(port);

        if(connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            cerr<<"Failed to connect to "<<host<<":"<<port<<endl;
            close(sock_fd);
            sock_fd = -1;
            return false;
        }

        connected = true;
        return true;
    }

    // Send a command and receive the response (with 3 second timeout)
    string sendCommand(const string& cmd){
        if(!connected) return "ERR: not connected\n";

        string msg = cmd + "\n";
        ssize_t sent = send(sock_fd, msg.c_str(), msg.size(), 0);
        if(sent <= 0){
            connected = false;
            return "ERR: send failed\n";
        }

        // Wait for response with 3 second timeout
        struct pollfd pfd;
        pfd.fd = sock_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        string response;
        // Read until we get a complete response (may be multiline for \status)
        while(true){
            int ret = poll(&pfd, 1, 3000); // 3 second timeout
            if(ret <= 0){
                if(ret == 0) return "ERR: server response timeout\n";
                return "ERR: poll error\n";
            }

            char buf[4096];
            ssize_t n = recv(sock_fd, buf, sizeof(buf)-1, 0);
            if(n <= 0){
                connected = false;
                return "ERR: connection lost\n";
            }
            buf[n] = '\0';
            response += string(buf, n);

            // Check if we have a complete response (ends with newline)
            if(!response.empty() && response.back() == '\n'){
                break;
            }
        }

        return response;
    }

    void disconnect(){
        if(sock_fd >= 0){
            close(sock_fd);
            sock_fd = -1;
        }
        connected = false;
    }

    bool isConnected() const { return connected; }
};
