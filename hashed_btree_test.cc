#include <memory>
#include <utility>
#include <cstdlib>

#include "gtest/gtest.h"

#include "hashed_btree.h"

TEST(hashed_btree, cache_align) {
	EXPECT_EQ(CACHELINE_SIZE, sizeof(hash_page));
	EXPECT_EQ(CACHELINE_SIZE, sizeof(btree_page::extent));
	EXPECT_EQ(CACHELINE_SIZE, sizeof(btree_page));
	EXPECT_EQ(1, sizeof(page));
}

TEST(hash_page, basic) {
	std::unique_ptr<hash_page> h(new hash_page);
	for (int i = 0; i < hash_page::kMaxItem; ++i) {
		ASSERT_TRUE(h->insert(std::make_pair(i, i + 1000))) << i;
		page::elem_t* e = h->find(i);
		ASSERT_NE(nullptr, e) << i;
		EXPECT_EQ(i, e->first) << i;
		EXPECT_EQ(i + 1000, e->second) << i;
	}
	ASSERT_FALSE(h->insert(std::make_pair(5000, 5000)));
}

btree_page* construct_btree_page() {
	hash_page* h = new hash_page;
	h->insert(std::make_pair(1,1));
	return hashed_btree::hash_page_to_btree_page(h);
}

TEST(btree_page, basic) {
	std::unique_ptr<btree_page> p(construct_btree_page());
	page::elem_t* t1 = p->find(1);
	ASSERT_NE(nullptr, t1);
	EXPECT_EQ(1, t1->second);

	ASSERT_EQ(nullptr, p->find(2)) << "non-exist 2";
	ASSERT_EQ(nullptr, p->find(-1)) << "non-exist -1";
	ASSERT_EQ(nullptr, p->find(374848)) << "non-exist 374848";

	p->insert(std::make_pair(2,2));
	page::elem_t* t2 = p->find(2);
	ASSERT_NE(nullptr, t2) << "after 2 insert";
	EXPECT_EQ(2, t2->second);
}

TEST(btree_page, extensive_sequential_insert) {
	std::unique_ptr<btree_page> p(construct_btree_page());
	const int kMaxNumInsert = btree_page::kMaxKey * (btree_page::extent::kMaxItem / 2) + btree_page::extent::kMaxItem;
	for (int i = 1; i < kMaxNumInsert; ++i) {
		ASSERT_TRUE(p->insert(std::make_pair(i + 1, i + 1000))) << i;
	}
	ASSERT_FALSE(p->insert(std::make_pair(5000, 5001)));

	ASSERT_NE(nullptr, p->find(1));
	for (int i = 1; i < kMaxNumInsert; ++i) {
		page::elem_t* t = p->find(i + 1);
		ASSERT_NE(nullptr, t) << i;
		EXPECT_EQ(i + 1, t->first);
		EXPECT_EQ(i + 1000, t->second);
	}
}

TEST(btree_page, extensive_random_insert) {
	std::vector<int> items(btree_page::kMaxKey * btree_page::extent::kMaxItem);
	srand(1);

	for (int rep = 0; rep < 100; ++rep) {
		for (int i = 0; i < items.size(); ++i) {
			items[i] = rand();
		}

		const int kMinNumInsert = btree_page::kMaxKey * (btree_page::extent::kMaxItem / 2) + btree_page::extent::kMaxItem - 1;

		std::unique_ptr<btree_page> p(construct_btree_page());
		int count = 1;
		for (int item : items) {
			if (!p->insert(std::make_pair(item, item + 1000))) {
				break;
			}
			++count;
		}

		EXPECT_LE(kMinNumInsert, count);

		ASSERT_NE(nullptr, p->find(1));
		for (int i = 0; i < count - 1; ++i) {
			page::elem_t* t;
			ASSERT_NE(nullptr, t = p->find(items[i])) << i;
			EXPECT_EQ(items[i] + 1000, t->second);
		}
	}
}

TEST(hash_page, conversion) {
	hash_page* hpage = new hash_page;
	for (int i = 1; i <= hash_page::kMaxItem; ++i) {
		hpage->insert(std::make_pair(i, i + 100));
	}

	btree_page* bpage = hashed_btree::hash_page_to_btree_page(hpage);
	for (int i = 1; i <= hash_page::kMaxItem; ++i) {
		page::elem_t* e = bpage->find(i);
		ASSERT_NE(nullptr, e) << i;
		EXPECT_EQ(i, e->first);
		EXPECT_EQ(i + 100, e->second);
	}
	bpage->release();
	delete bpage;
}

TEST(hashed_btree, resize) {
	hashed_btree m;
	for (uint64_t i = 1; i < 20; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}
	m.resize();
	for (uint64_t i = 1; i < 20; ++i) {
		hashed_btree::iterator iter = m.find(i);
		ASSERT_NE(m.end(), iter) << i;
		EXPECT_EQ(i, iter->first);
		EXPECT_EQ(1000ULL + i, iter->second);
	}
}

TEST(hashed_btree, MegaInsert) {
	hashed_btree m;

#ifdef _DEBUG
	const uint64_t kInsertIteration = 10000;
#else
	const uint64_t kInsertIteration = 1000000;
#endif 

	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}

	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		hashed_btree::iterator iter = m.find(i);
		ASSERT_NE(m.end(), iter) << i << "-th element";
		EXPECT_EQ(i, iter->first);
		EXPECT_EQ(1000ULL + i, iter->second);
	}
}

const uint64_t kInsertIteration = 100000000;

TEST(hashed_btree, MegaInsertBench) {
	hashed_btree m;

	for (uint64_t i = 1; i < kInsertIteration; ++i) {
		m.insert(std::make_pair(i, 1000ULL + i));
	}
	EXPECT_EQ(kInsertIteration - 1, m.size());
	double mega_capacity = static_cast<double>(m.size()) / m.num_page() / hash_page::kMaxItem;
	std::cout << "Capacity based on hash : " << mega_capacity << std::endl;
	std::cout << "Memory usage : " << m.num_page() * CACHELINE_SIZE / 1024 / 1024 << " MB" << std::endl;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
