#include "server.cpp"
#include <getopt.h>
#include <csignal>

// Phase 2: Multi-node Raft KV server entry point
// Usage: ./raftkv --id <N> --port <PORT> --data <DIR> --peers <host:port,host:port,...>

void printUsage(const char* prog){
    cerr<<"Usage: "<<prog<<" --id <node_id> --port <port> --data <data_dir> [--peers <host:port,...>]"<<endl;
    cerr<<"  --id     Node ID (required)"<<endl;
    cerr<<"  --port   Client port (required)"<<endl;
    cerr<<"  --data   Data directory for persistent state (default: ./data)"<<endl;
    cerr<<"  --peers  Comma-separated list of peer addresses (e.g., 127.0.0.1:5001,127.0.0.1:5002)"<<endl;
}

int main(int argc, char* argv[]){
    u_int64_t node_id = 0;
    int port = 0;
    string data_dir = "./data";
    string peers_str = "";

    static struct option long_options[] = {
        {"id",    required_argument, 0, 'i'},
        {"port",  required_argument, 0, 'p'},
        {"data",  required_argument, 0, 'd'},
        {"peers", required_argument, 0, 'e'},
        {"help",  no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while((opt = getopt_long(argc, argv, "i:p:d:e:h", long_options, NULL)) != -1){
        switch(opt){
            case 'i':
                node_id = atoi(optarg);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                data_dir = optarg;
                break;
            case 'e':
                peers_str = optarg;
                break;
            case 'h':
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    if(node_id == 0 || port == 0){
        printUsage(argv[0]);
        return 1;
    }

    // Parse peer list from comma-separated "host:port" pairs
    vector<pair<string,int>> peers;
    if(!peers_str.empty()){
        istringstream pss(peers_str);
        string token;
        while(getline(pss, token, ',')){
            size_t colon = token.find(':');
            if(colon == string::npos){
                cerr<<"Invalid peer format (expected host:port): "<<token<<endl;
                return 1;
            }
            string host = token.substr(0, colon);
            int peer_port = atoi(token.substr(colon+1).c_str());
            if(peer_port <= 0){
                cerr<<"Invalid peer port: "<<token<<endl;
                return 1;
            }
            peers.push_back({host, peer_port});
        }
    }

    // Set the global data directory
    DATA_DIR = data_dir;

    cout<<"[node "<<node_id<<"] starting"<<endl;
    for(size_t i=0; i<peers.size(); i++){
        cout<<"[node "<<node_id<<"] peer: "<<peers[i].first<<":"<<peers[i].second<<endl;
    }

    // Create server and run recovery
    RaftServer server(node_id, port, peers);

    if(!server.recover()){
        cerr<<"[node "<<node_id<<"] recovery failed, exiting"<<endl;
        return 1;
    }

    cout<<"[node "<<node_id<<"] entering FOLLOWER state, term 0"<<endl;

    // Start accepting client connections
    server.run();

    return 0;
}
