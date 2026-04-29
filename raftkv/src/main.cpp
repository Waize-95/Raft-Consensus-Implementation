#include "server.cpp"
#include <getopt.h>
#include <csignal>

// Phase 1: Single-node Raft KV server entry point
// Usage: ./raftkv --id <N> --port <PORT> --data <DIR>

void printUsage(const char* prog){
    cerr<<"Usage: "<<prog<<" --id <node_id> --port <port> --data <data_dir>"<<endl;
    cerr<<"  --id     Node ID (required)"<<endl;
    cerr<<"  --port   Client port (required)"<<endl;
    cerr<<"  --data   Data directory for persistent state (default: ./data)"<<endl;
}

int main(int argc, char* argv[]){
    u_int64_t node_id = 0;
    int port = 0;
    string data_dir = "./data";

    static struct option long_options[] = {
        {"id",   required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"data", required_argument, 0, 'd'},
        {"help", no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while((opt = getopt_long(argc, argv, "i:p:d:h", long_options, NULL)) != -1){
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

    // Set the global data directory
    DATA_DIR = data_dir;

    cout<<"[node "<<node_id<<"] starting"<<endl;

    // Create server and run recovery
    RaftServer server(node_id, port);

    if(!server.recover()){
        cerr<<"[node "<<node_id<<"] recovery failed, exiting"<<endl;
        return 1;
    }

    cout<<"[node "<<node_id<<"] entering LEADER state, term 0"<<endl;

    // Start accepting client connections
    server.run();

    return 0;
}
