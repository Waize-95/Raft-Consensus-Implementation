#include "client.cpp"

// raftkv-cli: Interactive client for the RaftKV server
// Usage: ./raftkv-cli <host> <port>

int main(int argc, char* argv[]){
    if(argc < 3){
        cerr<<"Usage: "<<argv[0]<<" <host> <port>"<<endl;
        return 1;
    }

    string host = argv[1];
    int port = atoi(argv[2]);

    RaftClient client;
    if(!client.connect_to(host, port)){
        cerr<<"Could not connect to "<<host<<":"<<port<<endl;
        return 1;
    }

    cout<<"connected to "<<host<<":"<<port<<endl;

    string line;
    while(true){
        cout<<"> ";
        cout.flush();

        if(!getline(cin, line)){
            // EOF
            break;
        }

        if(line.empty()) continue;

        // Check for QUIT locally
        string upper = line;
        for(auto& c : upper) c = toupper(c);
        if(upper == "QUIT"){
            client.sendCommand("QUIT");
            cout<<"Goodbye"<<endl;
            break;
        }

        string response = client.sendCommand(line);
        cout<<response;

        if(!client.isConnected()){
            cerr<<"Connection lost"<<endl;
            break;
        }
    }

    client.disconnect();
    return 0;
}
