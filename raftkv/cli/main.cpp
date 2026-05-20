#include "client.cpp"
#include <csignal>

// raftkv-cli: leader-aware client for the RaftKV cluster.
//
// Usage:
//   ./raftkv-cli <host> <port>                       (single node, legacy form)
//   ./raftkv-cli <id@host:port> [<id@host:port> ...] (full cluster, redirect-aware)
//
// Commands are read from stdin (interactive prompt or piped), one per line.
// PUT/GET/DELETE are routed to the leader automatically. \status and \dump
// are sent to whichever node the client is currently connected to.

static bool parseSpec(const string& spec, uint64_t& id, string& host, int& port){
    // form: id@host:port
    size_t at = spec.find('@');
    if(at == string::npos) return false;
    id = strtoull(spec.substr(0, at).c_str(), nullptr, 10);
    string rest = spec.substr(at+1);
    size_t colon = rest.find(':');
    if(colon == string::npos) return false;
    host = rest.substr(0, colon);
    port = atoi(rest.substr(colon+1).c_str());
    return id != 0 && port > 0;
}

int main(int argc, char* argv[]){
    signal(SIGPIPE, SIG_IGN);   // a server dropping us must not kill the client
    if(argc < 2){
        cerr << "Usage: " << argv[0] << " <host> <port>" << endl;
        cerr << "   or: " << argv[0] << " <id@host:port> [<id@host:port> ...]" << endl;
        return 1;
    }

    RaftClient client;

    if(argc == 3 && string(argv[2]).find('@') == string::npos &&
       string(argv[1]).find('@') == string::npos){
        // legacy form: host port  (id unknown -> use 0, redirect falls back to rotation)
        client.addServer(0, argv[1], atoi(argv[2]));
    }else{
        for(int i=1;i<argc;i++){
            uint64_t id; string host; int port;
            if(!parseSpec(argv[i], id, host, port)){
                cerr << "Invalid server spec (expected id@host:port): " << argv[i] << endl;
                return 1;
            }
            client.addServer(id, host, port);
        }
    }

    if(!client.ensureConnected()){
        cerr << "Could not connect to any server" << endl;
        return 1;
    }
    cout << "connected to " << client.describeCurrent() << endl;

    bool interactive = isatty(fileno(stdin));
    string line;
    while(true){
        if(interactive){ cout << "> "; cout.flush(); }
        if(!getline(cin, line)) break;
        if(line.empty()) continue;

        string up = line;
        for(auto& c : up) c = toupper(c);
        if(up == "QUIT"){ client.quit(); cout << "Goodbye" << endl; break; }

        cout << client.execute(line);
        cout.flush();
    }
    return 0;
}
