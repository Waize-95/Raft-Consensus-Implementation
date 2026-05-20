FROM ubuntu:22.04

RUN apt-get update && \
        apt-get install -y g++ make && \
        rm -rf /var/lib/apt/lists/*

WORKDIR /opt/raftkv

COPY . .

RUN make clean && make

CMD ["bash"]
