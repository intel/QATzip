################################################################
#   BSD LICENSE
#
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
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

QZIP_DIR = $(top_builddir)/utils
TEST_DIR = $(top_builddir)/test
QATZIP_LIB_DIR = $(top_builddir)/src
QATZIP_LIB = libqatzip.la
CODE_FORMATTING_BIN = $(top_builddir)/test/code_format_tests/format.sh

DIST_SUBDIRS = src utils test
SUBDIRS = src utils test

pkgincludedir = $(includedir)
pkginclude_HEADERS = include/qatzip.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = qatzip.pc
dist_man_MANS = \
                man/qzip.1

EXTRA_DIST = \
             LICENSE \
             README.md \
             include/qz_utils.h \
             src/xxhash.h	     \
             src/qatzip_internal.h \
             src/qatzip_page_table.h \
             utils/qzip.h \
             utils/qzstd.h \
             test/code_format_tests \
             test/performance_tests \
             docs/QATzip-man.pdf \
             config_file/


$(QATZIP_LIB):
	@make -C $(QATZIP_LIB_DIR)  $(QATZIP_LIB)

qzip: $(QATZIP_LIB)
	@make -C $(QZIP_DIR) qzip

qzip_obj_without_main:
	@make -C $(QZIP_DIR) qzip_obj_without_main

test:$(QATZIP_LIB)
	@make -C $(TEST_DIR) test

bt:$(QATZIP_LIB)
	@make -C $(TEST_DIR) bt
qzstd: $(QATZIP_LIB)
if QATZIP_LZ4S_POSTPROCESS_AC
	@make -C $(QZIP_DIR) qzstd
else
	@echo "Error : please enable lz4s postprocessing in configure!"
endif

check_code:
	@echo -e "\n\nPerforming code formatting tests..."
	@if ! $(CODE_FORMATTING_BIN); then                                   \
		echo "Check code format FAILED!!! :(";                       \
		exit 1;                                                      \
	fi
clean-local:
	@make -C $(TEST_DIR) clean

########################
# RPM package building #
########################

rpm: clean dist qatzip.spec
	@mkdir -p rpmbuild/BUILD rpmbuild/RPMS rpmbuild/SOURCES rpmbuild/SPECS rpmbuild/SRPMS
	@cp $(PACKAGE)-$(VERSION).tar.gz rpmbuild/SOURCES/
	@cp qatzip.spec rpmbuild/SPECS/
	@rpmbuild --define "_topdir $(abs_srcdir)/rpmbuild" -ba rpmbuild/SPECS/qatzip.spec

rpmclean:
	@rm -fr rpmbuild

.PHONY: rpm rpmclean libqatzip qzip test check_code \
	qzip_obj_without_main bt $(QATZIP_LIB) clean-local \
	qzstd
