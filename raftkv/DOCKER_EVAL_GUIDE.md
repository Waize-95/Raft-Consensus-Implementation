# Docker Evaluation Guide

This guide translates the official evaluation test ("What the Final Product Looks Like") into equivalent commands using the **Docker Compose** deployment option. This allows you to evaluate your cluster with actual network isolation without needing 5 separate terminals for the nodes.

## 1. Start the Cluster

Instead of opening 5 terminals, start the entire 5-node cluster in the background using Docker Compose:

```bash
docker compose up --build -d
```

## 2. Observe the Cluster Bootstrapping

To watch the same log outputs (elections, heartbeats, becoming leader) that you would normally see across the 5 terminals, follow the logs for all containers:

```bash
docker compose logs -f
```
Wait until you see one of the nodes output `got X votes, becoming LEADER`. Note which node became the leader (e.g., `raft3`).

*(You can leave this terminal running or press `Ctrl+C` to exit the log view—the cluster continues running in the background).*

## 3. Connect the Client

Open a second terminal. Because we mapped the Docker client ports to your host (`5001-5005`), you can connect exactly as before:

```bash
./raftkv-cli 1@127.0.0.1:5001 2@127.0.0.1:5002 3@127.0.0.1:5003 4@127.0.0.1:5004 5@127.0.0.1:5005
```

Issue your commands to ensure replication works:
```bash
> PUT x 1
> PUT y 2
> PUT z 3
> GET x
> \status
```

## 4. Kill the Leader (Simulating Failover)

Instead of going to the window of the leader node and pressing `Ctrl+C`, you kill the leader container directly using Docker. 

From a new terminal (leaving your client terminal open), stop whichever node became the leader in Step 2:

```bash
# Example if node 1 was the leader:
docker compose stop raft1
```

If you are watching `docker compose logs -f` in your log terminal, you will see the follower timeouts expire and a new leader get elected (just like in the original example).

## 5. Reconnect to the New Leader

Back in your client terminal, after the failover happens, exit the current unresponsive leader session and restart the client:

```bash
> QUIT
$ ./raftkv-cli 1@127.0.0.1:5001 2@127.0.0.1:5002 3@127.0.0.1:5003 4@127.0.0.1:5004 5@127.0.0.1:5005
```

*(Note: The `raftkv-cli` client is leader-aware, so even if you connect to the entire pool of 5 addresses, it will figure out who the new leader is automatically!).*

Run commands to ensure data persisted and new updates work under the new term:
```bash
> GET x
> GET y
> PUT w 4
```

## 6. Bring the Node Back (Catch-up)
You can optionally demonstrate that the dead node catches up when it returns:

```bash
docker compose start raft1
```
It will rejoin the cluster and quickly synchronize the missed `PUT w 4` command!

## Cleanup
When you are done with the evaluation:
```bash
docker compose down -v
```
*(The `-v` flag removes the Docker volumes so you start with a clean slate next time).*
