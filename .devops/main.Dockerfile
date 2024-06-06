ARG UBUNTU_VERSION=22.04

FROM ubuntu:$UBUNTU_VERSION as build

RUN apt-get update && \
    apt-get install -y build-essential git

WORKDIR /app

COPY . .

RUN make -j$(nproc) main

FROM ubuntu:$UBUNTU_VERSION as runtime

RUN apt-get update && \
    apt-get install -y libgomp1

COPY --from=build /app/llama /llama

ENV LC_ALL=C.utf8

ENTRYPOINT [ "/main" ]
