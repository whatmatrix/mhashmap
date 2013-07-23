all: mtest

gtest-all.o:
	c++ -O3 -stdlib=libc++ -std=c++11 -I../googletest-read-only/include -I../googletest-read-only ../gtest-1.6.0/src/gtest-all.cc -c

gtest: gtest-all.o
	ar rvs libgtest.a gtest-all.o

lookup3: lookup3.h lookup3.cc
	c++ -O3 -stdlib=libc++ -std=c++11 lookup3.cc -c

mhashmap_test: mhashmap.h mhashmap_test.cc
	c++ -O3 -stdlib=libc++ -std=c++11 mhashmap_test.cc -c -I../googletest-read-only/include

mtest: lookup3 mhashmap_test gtest
	c++ -O3 -stdlib=libc++ -std=c++11 -o mtest -lgtest -L. lookup3.o mhashmap_test.o

clean:
	rm -f libgtest.a gtest-all.o mhashmap_test.o lookup3.o
