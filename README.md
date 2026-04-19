# Raft-Consensus-Implementation
Distributed key-value store implementing the Raft consensus algorithm in C++
How it works: Using a famous algorithm called "Raft," the servers must constantly communicate and agree on the exact order of data commands. It is designed to perfectly survive extreme failures—as long as a majority (3 out of 5) of the servers are still alive, the cluster will continue to function without losing any data.

