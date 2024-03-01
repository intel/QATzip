################################################################
#   BSD LICENSE
#
#   Copyright(c) 2007-2024 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
################################################################

lib_LTLIBRARIES = libqatzip.la
libqatzip_la_SOURCES = \
                       qatzip.c \
                       qatzip_counter.c \
                       qatzip_gzip.c \
                       qatzip_mem.c \
                       qatzip_stream.c \
                       qatzip_sw.c \
                       qatzip_utils.c \
                       qatzip_lz4.c \
                       xxhash.c
libqatzip_la_CFLAGS = \
                      -I$(srcdir) \
                      -I$(top_srcdir)/include \
                      $(COMMON_CFLAGS) \
                      $(SAL_CFLAGS) \
                      $(ADF_CFLAGS) \
                      $(ICP_INCLUDE_CFLAGS) \
                      $(AM_CFLAGS)
libqatzip_la_LDFLAGS = \
                       -version-info $(LIBQATZIP_VERSION) \
		       $(ICP_LDFLAGS)

libqatzip_la_LIBADD= $(QATLIB_FLAGS) \
                     $(USDMLIB_FLAGS)
