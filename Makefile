#
# sparsebit_c/Makefile
#
# Copyright 2026 Louis Huemiller Jr.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

CC = gcc
CFLAGS = -O3 -std=c99 -Wall

all: includes libs utils tests

.PHONY: clean
clean:  ## Remove build artifacts
	rm -rf ./build/ *.pyc __pycache__/ *stackdump

.PHONY: clobber
clobber: clean  ## Remove built items and build artifacts
	rm -rf ./target/

.PHONY: includes
includes: ./target/include/sparsebit_c.h

.PHONY: libs
libs: ./target/lib/libsparsebit_c.a ./target/lib/libsparsebit_c_subtreenum.a

.PHONY: utils
utils: ./target/util/op_sieve

.PHONY: tests
tests: ./target/test/test_unit \
./target/test/test_adjacent \
./target/test/test_prand \
./target/test/test_subtreenum_unit \
./target/test/test_subtreenum_adjacent \
./target/test/test_subtreenum_prand \

# ---- Includes ----
./target/include/sparsebit_c.h: ./sparsebit_c.h
	mkdir -p ./target/include
	cp ./sparsebit_c.h ./target/include/sparsebit_c.h

# ---- Libs ----
./build/sparsebit_c.o: ./sparsebit_c.c ./sparsebit_c.h
	mkdir -p ./build
	$(CC) -c $(CFLAGS) -o ./build/sparsebit_c.o sparsebit_c.c

./target/lib/libsparsebit_c.a : ./build/sparsebit_c.o
	mkdir -p ./target/lib
	ar rc ./target/lib/libsparsebit_c.a ./build/sparsebit_c.o

./build/sparsebit_c_subtreenum.o: ./sparsebit_c.c ./sparsebit_c.h
	mkdir -p ./build
	$(CC) -c $(CFLAGS) -D SBIT_SUBTREENUM -o ./build/sparsebit_c_subtreenum.o \
		sparsebit_c.c

./target/lib/libsparsebit_c_subtreenum.a : ./build/sparsebit_c_subtreenum.o
	mkdir -p ./target/lib
	ar rc ./target/lib/libsparsebit_c_subtreenum.a \
		./build/sparsebit_c_subtreenum.o

# ---- Utils ----
./target/util/op_sieve: ./op_sieve.c ./target/lib/libsparsebit_c.a
	mkdir -p ./target/util
	$(CC) $(CFLAGS) -D SB_TEST -o ./target/util/op_sieve ./op_sieve.c \
		-L ./target/lib/ -l sparsebit_c

# ---- Tests ----
./target/test/test_unit: ./test_unit.c ./target/lib/libsparsebit_c.a
	mkdir -p ./target/test
	$(CC) $(CFLAGS) -D SB_TEST -D _POSIX_C_SOURCE=200809L \
		-o ./target/test/test_unit ./test_unit.c -L ./target/lib/ \
		-l sparsebit_c -l cunit

./target/test/test_adjacent: ./test_adjacent.c ./target/lib/libsparsebit_c.a
	$(CC) $(CFLAGS) -D SB_TEST -o ./target/test/test_adjacent \
		./test_adjacent.c -L ./target/lib/ -l sparsebit_c -l cunit

./target/test/test_prand: ./test_prand.c ./target/lib/libsparsebit_c.a
	$(CC) $(CFLAGS) -D SB_TEST -D _POSIX_C_SOURCE=200809L \
		-o ./target/test/test_prand ./test_prand.c -L ./target/lib/ \
		-l sparsebit_c -l cunit

./target/test/test_subtreenum_unit: ./test_unit.c \
./target/lib/libsparsebit_c_subtreenum.a
	mkdir -p ./target
	$(CC) $(CFLAGS) -D SB_TEST -D _POSIX_C_SOURCE=200809L \
		-o ./target/test/test_subtreenum_unit ./test_unit.c -L ./target/lib/ \
		-l sparsebit_c_subtreenum -l cunit

./target/test/test_subtreenum_adjacent: ./test_adjacent.c \
./target/lib/libsparsebit_c.a
	$(CC) $(CFLAGS) -D SB_TEST -o ./target/test/test_subtreenum_adjacent \
		./test_adjacent.c -L ./target/lib/ -l sparsebit_c_subtreenum -l cunit

./target/test/test_subtreenum_prand: ./test_prand.c \
./target/lib/libsparsebit_c.a
	$(CC) $(CFLAGS) -D SB_TEST -D _POSIX_C_SOURCE=200809L \
		-o ./target/test/test_subtreenum_prand ./test_prand.c \
		-L ./target/lib/ -l sparsebit_c_subtreenum -l cunit

.PHONY: smoketest
smoketest: tests
	./target/test/test_unit
	./target/test/test_adjacent
	./target/test/test_prand 0 999
	echo "---- subtreenum ----"
	./target/test/test_subtreenum_unit
	./target/test/test_subtreenum_adjacent
	./target/test/test_subtreenum_prand

.PHONY: presubmit
presubmit: smoketest
	./target/test/test_adjacent -n4
	./target/test/test_prand 0 99999
	echo "---- subtreenum ----"
	./target/test/test_subtreenum_adjacent -n4
	./target/test/test_subtreenum_prand 0 99999
