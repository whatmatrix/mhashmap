#ifndef MHASHMAP_H_
#define MHASHMAP_H_

#include <iostream>

#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <utility>

#include "lookup3.h"

#define HASHPAGE_SIZE 128

struct hash_function {
	typedef uint64_t key_t;
	size_t operator() (const key_t& k, int level) {
		return func(k + level * 2 + 1);
	};
	std::hash<uint64_t> func;
};

inline void bitmap_assign(uint8_t& bm, uint32_t index, bool v) {
	if (v) {
		bm |= 1 << index;
	} else {
		bm &= ~(static_cast<uint8_t>(1) << index);
	}
}

// TODO: insert optimization. insert with find. minimum evict.
// realloc.

inline bool bitmap_test(uint8_t& bm, uint32_t index) {
	return (bm & (1 << index)) > 0;
}

struct mhashpage {
	typedef uint64_t key_t;
	typedef uint64_t value_t;
	//static const uint64_t unused_key = static_cast<uint64_t>(-1LL);
	static const uint64_t unused_key = 0ULL;
	typedef std::pair<key_t, value_t> entry;
	struct context {
		//uint32_t num_hash_elements;
		uint16_t num_foreign_placed_elements;
		uint16_t rehashing_num_foreign_placed_elements;
		uint8_t used_bitmap;
		uint8_t foreign_bitmap;
		uint8_t padding0, padding1;
		mhashpage* overflow;
	} cxt;
	entry entries[(HASHPAGE_SIZE - sizeof(context)) / sizeof(entry)];
	static const int num_max_entries = (HASHPAGE_SIZE - sizeof(context)) / sizeof(entry);

	/*
	mhashpage() {
		cxt.num_hash_elements = 0;
		cxt.num_foreign_placed_elements = 0;
		for (entry& e : entries) {
			e.first = unused_key;
		}
	}
	*/

	bool overflow() const {
		return cxt.num_foreign_placed_elements != 0;
	}

	bool full() const {
		//return cxt.num_elements == num_max_entries;
		return cxt.used_bitmap == 0x7f;
		//for (const entry& e : entries) {
		//	if (e.first == unused_key) {
		//		return false;
		//	}
		//}
		//return true;
	}

	entry* find(const key_t& k) {
		if (cxt.used_bitmap == 0) {
			return nullptr;
		}
		//for (int i = 0; i < cxt.num_elements; ++i) {
		//	if (entries[i].first == k) {
		//		return &entries[i];
		//	}
		//}
		for (int i = 0; i < num_max_entries; ++i) {
			if (entries[i].first == k) {
				if (bitmap_test(cxt.used_bitmap, i)) {
					return &entries[i];
				}
			}
		}
		return nullptr;
	}

	bool insert(const entry& element, bool foreign) {
		check_bitmap();
		if (full()) {
			return false;
		}

		//bitmap_assign(cxt.foreign_bitmap, cxt.num_elements, foreign);
		//entries[cxt.num_elements++] = element;
		//++cxt.num_hash_elements;
		//return true;

		for (int i = 0; i < num_max_entries; ++i) {
			if (!bitmap_test(cxt.used_bitmap, i)) {
			//if (entries[i].first == unused_key) {
				// should move
				//assert(entries[i].first == unused_key);
				//assert(!bitmap_test(cxt.used_bitmap, i));
				entries[i] = element;
				bitmap_assign(cxt.used_bitmap, i, true);
				bitmap_assign(cxt.foreign_bitmap, i, foreign);

				check_bitmap();
				return true;
			}
		}
		return false;
	}

	void check_bitmap() {
		//for (int i = 0; i < num_max_entries; ++i) {
		//	assert(entries[i].first != unused_key || !bitmap_test(cxt.used_bitmap, i));
		//	assert(entries[i].first == unused_key || bitmap_test(cxt.used_bitmap, i));
		//}
	}

	void evict(entry& element, bool foreign, bool& foreign_evict, int count) {
		check_bitmap();
		//if (cxt.foreign_bitmap != 0) {
		//	// evict random foreign element
		//	for (int i = 0; i < cxt.num_elements; ++i) {
		//		if (bitmap_test(cxt.foreign_bitmap, i)) {
		//			std::swap(entries[i], element);
		//			bitmap_assign(cxt.foreign_bitmap, i, foreign);
		//			foreign_evict = true;
		//			return;
		//		}
		//	}
		//} else {
		//	const int target = 0;
		//	std::swap(entries[target], element);
		//	bitmap_assign(cxt.foreign_bitmap, target, foreign);
		//	foreign_evict = false;
		//	++cxt.num_foreign_placed_elements;
		//	return;
		//}

		int target;
		if (cxt.foreign_bitmap != 0) {
			// evict random foreign element
			target = -1;
			for (int i = 0; i < num_max_entries; ++i) {
				if (bitmap_test(cxt.foreign_bitmap, i)) {
					foreign_evict = true;
					target = i;
					break;
				}
			}
#ifdef DEBUG
			if (target == -1) {
				throw "error";
			}
#endif
		} else {
			target = count % num_max_entries;
			foreign_evict = false;
		}
		uint8_t old_bitmap = cxt.used_bitmap;
		check_bitmap();
		std::swap(entries[target], element);
		//assert(entries[target].first != unused_key);
		bitmap_assign(cxt.foreign_bitmap, target, foreign);

		check_bitmap();
	}

};

// TODO: STL conformity. iterator should have first and second instead of key() and value().
// 8 byte key and 8 byte value
class mhashmap {
public:
	typedef uint64_t key_t;
	typedef uint64_t value_t;

	class iterator {
	public:
		iterator(mhashpage::entry* e) : e_(e) {}

		void next();

		mhashpage::entry& operator *() { return *e_; }
		const mhashpage::entry& operator *() const { return *e_; }

		mhashpage::entry* operator ->() { return e_; }
		const mhashpage::entry* operator ->() const { return e_; }

		bool operator==(const iterator& rhs) const { return e_ == rhs.e_; }
		bool operator!=(const iterator& rhs) const { return e_ != rhs.e_; }

	private:
		mhashpage::entry* e_;
	};

	mhashmap() {
		page_ = reinterpret_cast<mhashpage*>(malloc(sizeof(mhashpage) * 2));
		capacity_ = 2;
		num_entries_ = 0;
		num_overflow_page_ = 0;
		num_overflow_element_ = 0;
		std::memset(page_, 0, sizeof(mhashpage) * 2);
	}

	mhashmap(int32_t capacity) {
		page_ = reinterpret_cast<mhashpage*>(malloc(sizeof(mhashpage) * capacity));
		capacity_ = capacity;
		num_entries_ = 0;
		num_overflow_page_ = 0;
		num_overflow_element_ = 0;
		std::memset(page_, 0, sizeof(mhashpage) * capacity);
	}

	~mhashmap() {
		delete[] page_;
	}

	size_t overflow_rate() const {
		size_t num_overflow = 0;
		for (mhashpage* iter = page_; iter < page_ + capacity_; ++iter) {
			if (iter->overflow()) {
				++num_overflow;
			}
		}
		return num_overflow + num_overflow_element_;
	}

	size_t overflow_page_element() const {
		return num_overflow_element_;
	}

	size_t capacity() const { return capacity_ * mhashpage::num_max_entries; }
	size_t size() const { return num_entries_; }

	void debug_find(int idx) {
		for (int i = 0; i < capacity_; ++i) {
			for (mhashpage::entry& e : page_[i].entries) {
				if (e.first == idx) {
					e.first = idx;
				}
			}
		}
	}

	int load_factor() const {
		return num_entries_ * 1000LL / mhashpage::num_max_entries / (capacity_ + num_overflow_page_);
	}

	void rebuild_or_rehash() {
		uint64_t current_load = load_factor();
		if (current_load > load_factor_) {
			rebuild();
		} else {
			//std::cout << "rehash" << std::endl;
			rehash();
		}
	}

	void rebuild_cuckoo(int32_t old_capacity, int i, int j) {
		uint32_t h1, h2;
		compute_hash(page_[i].entries[j].first, h1, h2);

		if (h1 == i) {
			return;
		}
		if (h2 == i) {
			if (h1 < old_capacity) {
				++page_[h1].cxt.rehashing_num_foreign_placed_elements;
			} else {
				++page_[h1].cxt.num_foreign_placed_elements;
			}
		}

		if (page_[h1].insert(page_[i].entries[j], false)) {
			bitmap_assign(page_[i].cxt.used_bitmap, j, false);
			//page_[i].entries[j].first = mhashpage::unused_key;
			return;
		}
		if (page_[h2].insert(page_[i].entries[j], true)) {
			bitmap_assign(page_[i].cxt.used_bitmap, j, false);
			//page_[i].entries[j].first = mhashpage::unused_key;
			if (h1 < old_capacity) { 
				++page_[h1].cxt.rehashing_num_foreign_placed_elements;
			} else {
				++page_[h1].cxt.num_foreign_placed_elements;
			}
			return;
		}

		mhashpage::entry evicted = page_[i].entries[j];
		page_[i].entries[j].first = mhashpage::unused_key;
		bitmap_assign(page_[i].cxt.used_bitmap, j, false);

		while (!rebuild_insert(old_capacity, evicted, h1, h2)) {
			rebuild_or_rehash();
			compute_hash(evicted.first, h1, h2);
		}
	}

	bool rebuild_insert(int32_t old_capacity, mhashpage::entry& evicted, uint32_t home_hash, uint32_t foreign_hash) {
		bool foreign = false;
		bool success = false;
		while (true) {
			int count = 0;
			while (count < MAX_ITERATION) {
				bool foreign_evict = false;

				if (foreign) {
					page_[foreign_hash].evict(evicted, foreign, foreign_evict, count);
					if (home_hash < old_capacity) {
						++page_[home_hash].cxt.rehashing_num_foreign_placed_elements;
					} else {
						++page_[home_hash].cxt.num_foreign_placed_elements;
					}
				} else {
					page_[home_hash].evict(evicted, foreign, foreign_evict, count);
				}

				compute_hash(evicted.first, home_hash, foreign_hash);
				if (page_[home_hash].insert(evicted, false)) {
					success = true;
					break;
				}
				if (page_[foreign_hash].insert(evicted, true)) {
					if (home_hash < old_capacity) {
						++page_[home_hash].cxt.rehashing_num_foreign_placed_elements;
					} else {
						++page_[home_hash].cxt.num_foreign_placed_elements;
					}
					success = true;
					break;
				}

				foreign = false;
				++count;
			}

			if (!success) {
				// Cannot succeeded within 20 retrial. Rebuild the hashmap.
				if (insert_overflow_page(home_hash, evicted)) {
					return true;
				}
				//std::cout << "rebuild failed";
				return false;
			}
			return true;
		}
		return true;
	}

	void rebuild() {
		//std::cout << "load factor : " << load_factor() << std::endl;
		//std::cout << "size : " << num_entries_ << std::endl;
		//std::cout << "capacity : " << capacity() << std::endl;

		if (capacity() > 50000000) {
			std::exit(-1);
		}

		int32_t old_capacity = capacity_;
		capacity_ *= 2;
		while (num_entries_ + num_overflow_element_ >= capacity_ * mhashpage::num_max_entries) {
			capacity_ *= 2;
		}

		page_ = reinterpret_cast<mhashpage*>(realloc(page_, sizeof(mhashpage) * capacity_));
		std::memset(&page_[old_capacity], 0, sizeof(mhashpage) * (capacity_ - old_capacity));

		for (int i = 0; i < old_capacity; ++i) {
			page_[i].cxt.num_foreign_placed_elements = 0;
			page_[i].cxt.foreign_bitmap = 0;
			if (page_[i].cxt.used_bitmap == 0) {
				continue;
			}
			uint64_t chk = page_[i].cxt.used_bitmap;
			uint64_t checker = 1;
			for (int j = 0; j < mhashpage::num_max_entries; ++j) {
				page_[i].check_bitmap();
				if ((chk & checker) == checker) {
				//if (bitmap_test(page_[i].cxt.used_bitmap, j)) {
				//if (page_[i].entries[j].first != mhashpage::unused_key) {
					rebuild_cuckoo(old_capacity, i, j);
				}
				checker <<= 1;
			}
			mhashpage* overflow = page_[i].cxt.overflow;
			if (overflow) {
				for (int j = 0; j < mhashpage::num_max_entries; ++j) {
					if (bitmap_test(overflow->cxt.used_bitmap, j)) {
					//if (overflow->entries[j].first != mhashpage::unused_key) {
						uint32_t home_hash, foreign_hash;
						compute_hash(overflow->entries[j].first, home_hash, foreign_hash);
						if (page_[home_hash].insert(overflow->entries[j], false)) {
							//overflow->entries[j].first = mhashpage::unused_key;
							bitmap_assign(overflow->cxt.used_bitmap, j, false);
							--num_overflow_element_;
							continue;
						}
						mhashpage::entry evicted = overflow->entries[j];
						//overflow->entries[j].first = mhashpage::unused_key;
						bitmap_assign(overflow->cxt.used_bitmap, j, false);
						--num_overflow_element_;
						while (!rebuild_insert(old_capacity, evicted, home_hash, foreign_hash)) {
							rebuild();
							compute_hash(evicted.first, home_hash, foreign_hash);
						}
					}
				}
				//if (overflow->entries[0].first == mhashpage::unused_key) {
				if (overflow->cxt.used_bitmap == 0) {
					free(overflow);
					page_[i].cxt.overflow = nullptr;
					--num_overflow_page_;
				}
			}
		}

		for (int i = 0; i < old_capacity; ++i) {
			page_[i].cxt.num_foreign_placed_elements = page_[i].cxt.rehashing_num_foreign_placed_elements;
			page_[i].cxt.rehashing_num_foreign_placed_elements = 0;
		}

	//	for (int i = 0; i < capacity_; ++i) {
	//		for (int j = 0; j < mhashpage::num_max_entries; ++j) {
	//			if (page_[i].entries[j].first != mhashpage::unused_key) {
	//				uint32_t h1, h2;
	//				compute_hash(page_[i].entries[j].first, h1, h2);
	//#ifdef DEBUG
	//				assert(i == h1 || i == h2);
	//#endif
	//				if (i == h2) {
	//					bitmap_assign(page_[i].cxt.foreign_bitmap, j, true);
	//					++page_[h1].cxt.num_foreign_placed_elements;
	//				}
	//			}
	//		}
	//	}
	}

	void rehash() {
		// TODO: implement rehash correctly.
		rebuild();
	}

	void compute_hash(const key_t& key, uint32_t& h1, uint32_t& h2) {
		//h1 = 0;
		//h2 = 0;
		//hashlittle2(&key, sizeof(key_t), &h1, &h2);
		////h1 = static_cast<uint32_t>(h1_(key, 0));
		////h2 = static_cast<uint32_t>(h1_(key, 1));
		//if (h1 == h2) {
		//	h2 = ~h2;
		//	if (h1 == h2) {
		//		++h2;
		//	}
		//}

		h1 = (key + 343741) * 1203804 % 99981599 ;
		h2 = (key + 571237) * 571723 % 999815601;
		h1 %= capacity_;
		h2 %= capacity_;
	}

	void compute_four_hash(const key_t& key, uint32_t& h1, uint32_t& h2, uint32_t& old_h1, uint32_t& old_h2) {
		//h1 = 0;
		//h2 = 0;
		//hashlittle2(&key, sizeof(key_t), &h1, &h2);
		h1 = h1_(key, 0);
		h2 = h1_(key, 1);
		if (h1 == h2) {
			h2 = ~h2;
			if (h1 == h2) {
				++h2;
			}
		}

		old_h1 = h1 % (capacity_ / 2);
		old_h2 = h2 % (capacity_ / 2);

		h1 %= capacity_;
		h2 %= capacity_;
	}

	bool insert_overflow_page(uint32_t home_hash, const mhashpage::entry& element) {
		mhashpage* home = &page_[home_hash];
		if (!home->cxt.overflow) {
			++num_overflow_page_;
			home->cxt.overflow = reinterpret_cast<mhashpage*>(malloc(sizeof(mhashpage)));
			memset(home->cxt.overflow, 0, sizeof(mhashpage));
		} else if (home->cxt.overflow->full()) {
			//std::cout << "overflow insert fail" << std::endl;
			return false;
		}
		home->cxt.overflow->insert(element, false);
		++num_overflow_element_;
		return true;
	}

	void insert(const mhashpage::entry& element) {
		uint32_t home_hash;
		uint32_t foreign_hash;

		compute_hash(element.first, home_hash, foreign_hash);

		mhashpage* home_page  = &page_[home_hash];

		if (home_page->find(element.first) != nullptr) {
			// duplicate
			return;
		}
		if (home_page->overflow()) {
			mhashpage* foreign_page = &page_[foreign_hash];
			if (foreign_page->find(element.first) != nullptr) {
				// duplicate
				return;
			}
		}
		if (home_page->cxt.overflow) {
			if (home_page->cxt.overflow->find(element.first) != nullptr) {
				return;
			}
		}
		if (home_page->insert(element, false)) {
			++num_entries_;
			return;
		}

		mhashpage::entry evicted;
		if (home_page->cxt.foreign_bitmap != 0) {
			int target;
			for (int i = 0; i < mhashpage::num_max_entries; ++i) {
				if (bitmap_test(home_page->cxt.foreign_bitmap, i)) {
					target = i;
					break;
				}
			}
			evicted = home_page->entries[target];
			home_page->entries[target] = element;
			bitmap_assign(home_page->cxt.foreign_bitmap, target, false);

			compute_hash(evicted.first, home_hash, foreign_hash);
			home_page = &page_[home_hash];
			--home_page->cxt.num_foreign_placed_elements;
			//assert(page_[home_hash].cxt.num_foreign_placed_elements >= 0);
			if (home_page->insert(evicted, false)) {
				++num_entries_;
				return;
			}
		} else {
			mhashpage* foreign_page = &page_[foreign_hash];
			if (foreign_page->insert(element, true)) {
				++home_page->cxt.num_foreign_placed_elements;
				++num_entries_;
				return;
			}
			evicted = element;
		}

		bool foreign = false;
		bool success = false;
		while (true) {
			int count = 0;
			while (count < MAX_ITERATION) {
				bool foreign_evict = false;

				if (foreign) {
					page_[foreign_hash].evict(evicted, foreign, foreign_evict, count);
					++page_[home_hash].cxt.num_foreign_placed_elements;
				} else {
					page_[home_hash].evict(evicted, foreign, foreign_evict, count);
				}

				compute_hash(evicted.first, home_hash, foreign_hash);
				if (foreign_evict) {
					--page_[home_hash].cxt.num_foreign_placed_elements;
					//assert(page_[home_hash].cxt.num_foreign_placed_elements >= 0);
					if (page_[home_hash].insert(evicted, false)) {
						success = true;
						break;
					}
				} else {
					if (page_[foreign_hash].insert(evicted, true)) {
						++page_[home_hash].cxt.num_foreign_placed_elements;
						success = true;
						break;
					}
				}

				foreign = !foreign_evict;
				++count;
			}

			if (!success) {
				// Cannot succeeded within 20 retrial. Rebuild the hashmap.
				if (load_factor() >= load_factor_ || !insert_overflow_page(home_hash, evicted)) {
					rebuild_or_rehash();
					compute_hash(evicted.first, home_hash, foreign_hash);

					if (!page_[home_hash].insert(evicted, false)) {
						foreign = false;
						continue;
					}
				}
			}
			++num_entries_;
			return;
		}
	}


	iterator find(const key_t& k) {
		uint32_t h1, h2;
		compute_hash(k, h1, h2);

		mhashpage& first_page = page_[h1];
		mhashpage::entry* e = first_page.find(k);
		if (e != nullptr) {
			return iterator(e);
		}

		if (first_page.cxt.overflow) {
			e = first_page.cxt.overflow->find(k);
			if (e != nullptr) {
				return iterator(e);
			}
			}

		if (!first_page.overflow()) {
			return end();
		}

		mhashpage& second_page = page_[h2];
		e = second_page.find(k);
		if (e != nullptr) {
			return iterator(e);
		}
		return end();
	}

	bool erase(const key_t& k);

	iterator begin();
	iterator end() {
		return iterator(&page_[capacity_].entries[0]);
	}

private:
	static const int MAX_ITERATION = 5;
	// 70% occupancy
	static const uint32_t load_factor_ = 700;

	mhashpage* page_;
	int32_t num_entries_;
	int32_t capacity_;
	int32_t num_overflow_page_;
	int32_t num_overflow_element_;
	hash_function h1_;
};

#endif  // MHASHMAP_H_
