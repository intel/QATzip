#==========================================================================
#                                                                         \
#                                                                         \
#   BSD LICENSE                                                           \
#                                                                         \
#   Copyright(c) 2024 Intel Corporation.                                  \
#   All rights reserved.                                                  \
#                                                                         \
#   Redistribution and use in source and binary forms, with or without    \
#   modification, are permitted provided that the following conditions    \
#   are met:                                                              \
#                                                                         \
#     * Redistributions of source code must retain the above copyright    \
#       notice, this list of conditions and the following disclaimer.     \
#     * Redistributions in binary form must reproduce the above copyright \
#       notice, this list of conditions and the following disclaimer in   \
#       the documentation and/or other materials provided with the        \
#       distribution.                                                     \
#     * Neither the name of Intel Corporation nor the names of its        \
#       contributors may be used to endorse or promote products derived   \
#       from this software without specific prior written permission.     \
#                                                                         \
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   \
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     \
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR \
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  \
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, \
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT      \
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, \
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY \
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT   \
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE \
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  \
#                                                                         \
#                                                                         \
#==========================================================================

ARG UBUNTU_BASE=ubuntu:22.04
FROM ${UBUNTU_BASE} AS builder

ARG QATLIB_VERSION="24.02.0"
ARG QATZIP_VERSION="v1.3.1"
ARG QATZSTD_PLUG_VERSION="v0.2.0"
ARG ZSTD_VERSION="v1.5.5"
ARG GID
ENV DEBIAN_FRONTEND=noninteractive


# Install required packages
RUN apt-get update && \
    apt-get install -y apt-utils

# Upgrade all other packages
RUN apt-get upgrade -y && \
    apt-get install -y \
    libudev-dev \
    make \
    gcc \
    g++ \
    nasm \
    pkg-config \
    libssl-dev \
    libpcre3-dev \
    zlib1g-dev \
    libreadline-dev \
    lua5.4 \
    liblua5.4-dev \
    autoconf \
    automake \
    cmake \
    git \
    ca-certificates \
    liblz4-dev \
    lz4 \
    p7zip-full \
    libnuma-dev \
    numactl \
    gzip \
    libtool && \
    git clone --depth 1 -b $QATLIB_VERSION https://github.com/intel/qatlib && \
    git clone --depth 1 -b $QATZIP_VERSION https://github.com/intel/QATzip.git && \
    git clone --depth 1 -b $QATZSTD_PLUG_VERSION https://github.com/intel/QAT-ZSTD-Plugin.git && \
    git clone --depth 1 -b $ZSTD_VERSION https://github.com/facebook/zstd.git && \
    git clone --depth 1 -b $ZSTD_VERSION https://github.com/facebook/zstd.git zstd_with_plugin

# Create a non-root user and group
RUN groupadd -r appuser && useradd -r -g appuser -s /bin/bash appuser

# Build and Install QATLib
WORKDIR /qatlib
RUN ./autogen.sh && \
    ./configure --enable-systemd=no && \
    make -j && \
    make install samples-install && \
    groupadd qat -g ${GID} && \
    usermod -a -G qat appuser


#Build ZSTD
WORKDIR /zstd/lib
RUN make -j
WORKDIR /zstd/
RUN make install

# Build & Install QATzip
WORKDIR /QATzip
RUN ./autogen.sh && \
    ./configure --enable-lz4s-postprocessing && \
    make qzstd && make install -j

#Build ZSTD_WITH_PLUGIN
WORKDIR /zstd_with_plugin/lib
RUN make -j

#Build and Install qatzstdplugin
WORKDIR /QAT-ZSTD-Plugin
RUN make clean && \
    make ENABLE_USDM_DRV=0 DEBUGLEVEL=2 -j

#Install ZSTD 
WORKDIR /zstd_with_plugin/
RUN make QAT_SUPPORT=1 SEQPRODLIB=/QAT-ZSTD-Plugin/src PREFIX=/zstd_with_plugin/ -j && \
    make install 

#Added to remove libc library for vulnerability issue
RUN apt-get purge -y linux-libc-dev

FROM ${UBUNTU_BASE}

RUN apt-get update && \
    apt-get upgrade -y

COPY --from=builder /usr/local/lib/libqat.so.4.2.0 /usr/lib/
COPY --from=builder /usr/local/lib/libusdm.so.0.1.0 /usr/lib/
COPY --from=builder /usr/local/lib/libqatzip.so.3.2.0 /usr/lib/
COPY --from=builder /usr/local/bin/qzip /usr/bin/qzip
COPY --from=builder /usr/local/bin/qatzip-test /usr/bin/qatzip-test
COPY --from=builder /QATzip/utils/qzstd /usr/bin/qzstd
COPY --from=builder /usr/lib/p7zip/7z /usr/bin/7z
COPY --from=builder /usr/lib/p7zip/7za /usr/bin/7za
COPY --from=builder /usr/lib/p7zip/7zr /usr/bin/7zr
COPY --from=builder /zstd_with_plugin/programs/zstd/ /usr/bin/
COPY --from=builder /usr/bin/lz4 /usr/bin/lz4
COPY --from=builder /usr/bin/numactl /usr/bin/numactl
COPY --from=builder /usr/bin/gzip /usr/bin/gzip
COPY --from=builder /usr/lib/x86_64-linux-gnu/libnuma.so.1 /usr/lib/x86_64-linux-gnu/libnuma.so.1
COPY --from=builder /etc/group /etc/group
COPY --from=builder /etc/passwd /etc/passwd

RUN ldconfig

#Switch to non-root user
USER appuser

ENV QAT_POLICY=1
ENV LD_LIBRARY_PATH="/usr/bin"
