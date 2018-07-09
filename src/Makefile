################################################################
#   BSD LICENSE
#
#   Copyright(c) 2007-2018 Intel Corporation. All rights reserved.
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

LIB_SOURCES = qatzip.c qatzip_counter.c qatzip_gzip.c \
              qatzip_sw.c qatzip_mem.c qatzip_utils.c \
			  qatzip_stream.c

OBJECTS = $(foreach file,$(LIB_SOURCES),$(file:.c=.o))

LDFLAGS += -shared -Wl,-soname,$(QATZIP_LIB_SHARED)
LIB_STATIC_FLAGS = rcs

all: $(QATZIP_LIB_STATIC) $(QATZIP_LIB_SHARED)

# Create the shared library
$(QATZIP_LIB_SHARED): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS) $(LIBADD)

# Create the static library
$(QATZIP_LIB_STATIC): $(OBJECTS)
	$(AR) $(LIB_STATIC_FLAGS) $@ $^

#Compile the object files with the CFLAGS
$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

clean:
	$(RM) *.o $(QATZIP_LIB_STATIC) $(QATZIP_LIB_SHARED)

.PHONY: all clean
