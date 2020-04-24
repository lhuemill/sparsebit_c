#
# sparsebit_c/Makefile
#

all: libs tests

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

.PHONY: libs
libs: ./build/ ./target/lib/ ./target/lib/sparsebit_c.a
./target/lib/sparsebit_c.a : ./target/lib/ ./build/sparsebit_c.o
	ar rc ./target/lib/sparsebit_c.a ./build/sparsebit_c.o

./build/sparsebit_c.o: ./build/ ./sparsebit_c.c ./sparsebit_c.h
	gcc -c -std=c99 -o ./build/sparsebit_c.o sparsebit_c.c




.PHONY: tests
tests: ./target/ ./target/test_unit ./target/test_prand
./target/test_unit: ./test_unit.c ./target/lib/sparsebit_c.a
	gcc -std=c99 -D SB_TEST -o ./target/test_unit ./test_unit.c ./target/lib/sparsebit_c.a -l cunit
./target/test_prand: ./test_prand.c ./target/lib/sparsebit_c.a
	gcc -std=c99 -D SB_TEST -o ./target/test_prand ./test_prand.c ./target/lib/sparsebit_c.a -l cunit

.PHONY: smoketest
smoketest: tests
	./target/test_unit
	./target/test_prand 0 1000
