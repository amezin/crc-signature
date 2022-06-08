FROM ubuntu:focal AS base

RUN dpkg --add-architecture i386 && \
    apt-get update -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        libboost-program-options1.71.0:i386

FROM base AS build

RUN apt-get update -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        cmake \
        make \
        g++-10-multilib \
        libboost-program-options1.71-dev:i386

ENV CC=gcc-10 CXX=g++-10 CFLAGS=-m32 CXXFLAGS=-m32

COPY src /src/signature/src
COPY CMakeLists.txt /src/signature/

RUN mkdir /build && \
    cd /build && \
    cmake -DCMAKE_BUILD_TYPE=Release /src/signature/ && \
    make -j$(nproc) install

FROM base

COPY --from=build /usr/local/bin/signature /usr/local/bin
ENTRYPOINT ["/usr/local/bin/signature"]
