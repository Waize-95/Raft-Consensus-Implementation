FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . /app
RUN make raftkv raftkv-cli

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app/raftkv /app/raftkv-cli /app/
# id, ports, peers and data dir are supplied per-service in docker-compose.yml
ENTRYPOINT ["/app/raftkv"]
