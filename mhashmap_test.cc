#include <algorithm>
#include <random>
#include <unordered_map>
#include <iostream>

#include "gtest/gtest.h"

#include "mhashmap.h"

void BM_MHashMap_RandomInsert() {
	mhashmap m;

	const int kMaxInsertion = 1000000;
	std::default_random_engine eng;
	std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max() - 1);	
	for (int i = 0; i < kMaxInsertion; ++i) {
		m.insert(std::make_pair(dist(eng), dist(eng)));
	}
}

TEST(MHASHMAP, CacheAlign) {
	EXPECT_EQ(HASHPAGE_SIZE, sizeof(mhashpage));
	mhashpage page[2];
	EXPECT_EQ(HASHPAGE_SIZE * 2, sizeof(page));
	mhashpage page3[3];
	EXPECT_EQ(HASHPAGE_SIZE * 3, sizeof(page3));
	mhashpage page7[7];
	EXPECT_EQ(HASHPAGE_SIZE * 7, sizeof(page7));
}

TEST(MHASHMAP, SimpleInsertAndFind) {
	mhashmap m;
	m.insert(std::make_pair(5ULL, 1000ULL));

	mhashmap::iterator iter = m.find(5);
	ASSERT_NE(m.end(), iter);
	EXPECT_EQ(5ULL, iter->first);
	EXPECT_EQ(1000ULL, iter->second);
	
	EXPECT_EQ(m.end(), m.find(1000));
}

TEST(MHASHMAP, Rebuild) {
	mhashmap m;

	for (uint64_t i = 1; i < 10; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}

	for (uint64_t i = 1; i < 10; ++i) {
		mhashmap::iterator iter = m.find(i);
		ASSERT_NE(m.end(), iter) << i << "-th element";
		EXPECT_EQ(i, iter->first);
		EXPECT_EQ(1000ULL + i, iter->second);
	}
}

TEST(MHASHMAP, MegaInsert) {
	mhashmap m;

	for (uint64_t i = 1; i < 1000000; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}

	m.debug_find(23409);

	for (uint64_t i = 1; i < 1000000; ++i) {
		mhashmap::iterator iter = m.find(i);
		ASSERT_NE(m.end(), iter) << i << "-th element";
		EXPECT_EQ(i, iter->first);
		EXPECT_EQ(1000ULL + i, iter->second);
	}
}

const uint64_t kInsertIteration = 30000000;

size_t mega_capacity = 6291455;

TEST(MHASHMAP, MegaInsertBench) {
	mhashmap m;

	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}
	EXPECT_EQ(kInsertIteration - 1, m.size());
	std::cout << "Capacity : " << m.capacity() / mhashpage::num_max_entries << std::endl;
	std::cout << "Load Factor : " << m.size() * 1000 / m.capacity() << std::endl;
	std::cout << "Overflow Rate : " << 100.0 * m.overflow_rate() / m.size() << "%" << std::endl;
	mega_capacity = m.capacity() / mhashpage::num_max_entries;
	std::cout << "Memory usage : " << m.capacity() / mhashpage::num_max_entries * sizeof(mhashpage) / 1024 / 1024 << " MB" << std::endl;
}

TEST(MHASHMAP, MegaLookupBench) {
	mhashmap m;

	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}

	std::default_random_engine eng;
	std::uniform_int_distribution<uint64_t> dist(1, kInsertIteration - 1);	

	for (uint64_t i = 0; i < kInsertIteration * 3; ++i) {
		EXPECT_NE(m.end(), m.find(dist(eng)));
	}
}

TEST(unordered_map, MegaInsertBench) {
	std::unordered_map<uint64_t, uint64_t> m;
	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}
}

TEST(MHASHMAP, MegaInsertReserveBench) {
	mhashmap m(mega_capacity);

	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}
	std::cout << "Capacity : " << m.capacity() / mhashpage::num_max_entries << std::endl;
}

TEST(unordered_map, MegaInsertReserveBench) {
	std::unordered_map<uint64_t, uint64_t> m;
	m.reserve(static_cast<size_t>(kInsertIteration * 2.5));
	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}
}

TEST(unordered_map, MegaLookupBench) {
	std::unordered_map<uint64_t, uint64_t> m;
	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}

	std::default_random_engine eng;
	std::uniform_int_distribution<uint64_t> dist(1, kInsertIteration - 1);	

	for (uint64_t i = 0; i < kInsertIteration * 3; ++i) {
		EXPECT_NE(m.end(), m.find(dist(eng)));
	}
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
