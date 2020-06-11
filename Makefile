#
# sparsebit_c/Makefile
#
# Copyright 2020 Louis Huemiller Jr.
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

all: libs tests util

.PHONY: clean
clean:  ## Remove build artifacts
	rm -rf ./build/ *.pyc __pycache__/ *stackdump

.PHONY: clobber
clobber: clean  ## Remove built items and build artifacts
	rm -rf ./target/

./build/:
	mkdir -p ./build/

./target/:
	mkdir -p ./target/

./target/lib/:
	mkdir -p ./target/lib/

./target/util/:
	mkdir -p ./target/util/

.PHONY: libs
libs: ./build/ ./target/lib/ ./target/lib/libsparsebit_c.a
./target/lib/libsparsebit_c.a : ./target/lib/ ./build/sparsebit_c.o
	ar rc ./target/lib/libsparsebit_c.a ./build/sparsebit_c.o

./build/sparsebit_c.o: ./build/ ./sparsebit_c.c ./sparsebit_c.h
	gcc -c -std=c99 -o ./build/sparsebit_c.o sparsebit_c.c

.PHONY: tests
tests: ./target/ ./target/test_unit ./target/test_prand
./target/test_unit: ./test_unit.c ./target/lib/libsparsebit_c.a
	gcc -std=c99 -D SB_TEST -o ./target/test_unit ./test_unit.c -L ./target/lib/ -l sparsebit_c -l cunit
./target/test_prand: ./test_prand.c ./target/lib/libsparsebit_c.a
	gcc -std=c99 -D SB_TEST -o ./target/test_prand ./test_prand.c -L ./target/lib/ -l sparsebit_c -l cunit

.PHONY: util
util: ./target/ ./target/util/ ./target/util/op_sieve
./target/util/op_sieve: ./op_sieve.c ./target/lib/libsparsebit_c.a
	gcc -g -std=c99 -D SB_TEST -o ./target/util/op_sieve ./op_sieve.c -L ./target/lib/ -l sparsebit_c

.PHONY: smoketest
smoketest: tests
	./target/test_unit
	./target/test_prand 0 999

.PHONY: presubmit
presubmit: smoketest
	./target/test_prand 0 99999
