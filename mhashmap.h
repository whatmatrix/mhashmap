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

// TODO:
// foreign bitmap manipulation.
// organize duplicated code. insert and reinsert, cuckoo.
// neat hash functions and being able to rehash.

struct bitmap_t {
	void assign(int index, bool v) {
		if (v) {
			set(index);
		} else {
			clear(index);
		}
	}

	void set(int index) {
		bm |= static_cast<uint8_t>(1) << index;
	}

	void clear(int index) {
		bm &= ~(static_cast<uint8_t>(1) << index);
	}

	bool test(int index) const {
		return (bm & (static_cast<uint8_t>(1) << index)) > 0;
	}

	bool empty() const {
		return bm == 0;
	}
	uint8_t bm;
};

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
		bitmap_t used_bitmap;
		bitmap_t foreign_bitmap;
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
		return cxt.used_bitmap.bm == 0x7f;
		//for (const entry& e : entries) {
		//	if (e.first == unused_key) {
		//		return false;
		//	}
		//}
		//return true;
	}

	bool empty() const {
		return cxt.used_bitmap.empty();
	}

	entry* find(const key_t& k) {
		if (cxt.used_bitmap.bm == 0) {
			return nullptr;
		}
		//for (int i = 0; i < cxt.num_elements; ++i) {
		//	if (entries[i].first == k) {
		//		return &entries[i];
		//	}
		//}
		for (int i = 0; i < num_max_entries; ++i) {
			if (entries[i].first == k) {
				if (cxt.used_bitmap.test(i)) {
					return &entries[i];
				}
			}
		}
		return nullptr;
	}

	void clear(int index) {
		cxt.used_bitmap.clear(index);
	}

	bool has(int index) const {
		return cxt.used_bitmap.test(index);
	}

	bool can_evict_foreign() const {
		return !cxt.foreign_bitmap.empty();
	}

	void evict_foreign(const entry& element, entry& evicted) {
		int target;
		for (int i = 0; i < mhashpage::num_max_entries; ++i) {
			if (cxt.foreign_bitmap.test(i)) {
				target = i;
				break;
			}
		}
		evicted = entries[target];
		entries[target] = element;
		cxt.foreign_bitmap.clear(target);
	}

	bool insert(const entry& element, bool foreign) {
		if (full()) {
			return false;
		}

		//bitmap_assign(cxt.foreign_bitmap.bm, cxt.num_elements, foreign);
		//entries[cxt.num_elements++] = element;
		//++cxt.num_hash_elements;
		//return true;

		for (int i = 0; i < num_max_entries; ++i) {
			if (!cxt.used_bitmap.test(i)) {
			//if (entries[i].first == unused_key) {
				// should move
				//assert(entries[i].first == unused_key);
				//assert(!bitmap_test(cxt.used_bitmap.bm, i));
				entries[i] = element;
				cxt.used_bitmap.set(i);
				cxt.foreign_bitmap.assign(i, foreign);

				return true;
			}
		}
		return false;
	}

	void evict(entry& element, bool foreign, bool& foreign_evict, int count) {
		int target;
		if (!cxt.foreign_bitmap.empty()) {
			// evict random foreign element
			for (int i = 0; i < num_max_entries; ++i) {
				if (cxt.foreign_bitmap.test(i)) {
					foreign_evict = true;
					target = i;
					break;
				}
			}
		} else {
			target = count % num_max_entries;
			foreign_evict = false;
		}
		std::swap(entries[target], element);
		cxt.foreign_bitmap.assign(target, foreign);
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
		last_increment_ = 0;
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
			page_[h2].cxt.foreign_bitmap.set(j);
			page_[h1].cxt.foreign_bitmap.clear(j);
			if (h1 < old_capacity) {
				++page_[h1].cxt.rehashing_num_foreign_placed_elements;
			} else {
				++page_[h1].cxt.num_foreign_placed_elements;
			}
		}

		if (page_[h1].insert(page_[i].entries[j], false)) {
			page_[i].clear(j);
			//page_[i].entries[j].first = mhashpage::unused_key;
			return;
		}
		if (page_[h2].insert(page_[i].entries[j], true)) {
			page_[i].clear(j);
			//page_[i].entries[j].first = mhashpage::unused_key;
			if (h1 < old_capacity) { 
				++page_[h1].cxt.rehashing_num_foreign_placed_elements;
			} else {
				++page_[h1].cxt.num_foreign_placed_elements;
			}
			return;
		}

		mhashpage::entry evicted = page_[i].entries[j];
		page_[i].clear(j);

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

	void increment_capacity() {
		capacity_ *= 2;
	}

	void rebuild_overflow_page(int32_t old_capacity, int i, mhashpage* overflow) {
		for (int j = 0; j < mhashpage::num_max_entries; ++j) {
			if (overflow->has(j)) {
				uint32_t home_hash, foreign_hash;
				compute_hash(overflow->entries[j].first, home_hash, foreign_hash);
				if (page_[home_hash].insert(overflow->entries[j], false)) {
					overflow->clear(j);
					--num_overflow_element_;
					continue;
				}
				mhashpage::entry evicted = overflow->entries[j];
				overflow->clear(j);
				--num_overflow_element_;
				while (!rebuild_insert(old_capacity, evicted, home_hash, foreign_hash)) {
					rebuild();
					compute_hash(evicted.first, home_hash, foreign_hash);
				}
			}
		}
		if (overflow->cxt.used_bitmap.bm == 0) {
			free(overflow);
			page_[i].cxt.overflow = nullptr;
			--num_overflow_page_;
		}
	}


	void rebuild() {
		int32_t old_capacity = capacity_;

		increment_capacity();
		while (num_entries_ + num_overflow_element_ >= capacity_ * mhashpage::num_max_entries) {
			increment_capacity();
		}

		page_ = reinterpret_cast<mhashpage*>(realloc(page_, sizeof(mhashpage) * capacity_));
		std::memset(&page_[old_capacity], 0, sizeof(mhashpage) * (capacity_ - old_capacity));

		for (int i = 0; i < old_capacity; ++i) {
			page_[i].cxt.num_foreign_placed_elements = 0;
			if (page_[i].empty()) {
				continue;
			}
			uint64_t chk = page_[i].cxt.used_bitmap.bm;
			uint64_t checker = 1;
			for (int j = 0; j < mhashpage::num_max_entries; ++j) {
				if ((chk & checker) == checker) {
				//if (bitmap_test(page_[i].cxt.used_bitmap.bm, j)) {
				//if (page_[i].entries[j].first != mhashpage::unused_key) {
					rebuild_cuckoo(old_capacity, i, j);
				}
				checker <<= 1;
			}
			mhashpage* overflow = page_[i].cxt.overflow;
			if (overflow) {
				rebuild_overflow_page(old_capacity, i, overflow);
			}
		}

		for (int i = 0; i < old_capacity; ++i) {
			page_[i].cxt.num_foreign_placed_elements = page_[i].cxt.rehashing_num_foreign_placed_elements;
			page_[i].cxt.rehashing_num_foreign_placed_elements = 0;
		}

	}

	void rehash() {
		// TODO: implement rehash correctly.
		rebuild();
	}

	void compute_hash(const key_t& key, uint32_t& h1, uint32_t& h2) {
		//h1 = 0;
		//h2 = 0;
		//hashlittle2(&key, sizeof(key_t), &h1, &h2);
		h1 = static_cast<uint32_t>(h1_(key, 0));
		////h2 = static_cast<uint32_t>(h1_(key, 1));
		//h1 = (key + 343741) * 1203804 % 99981599 ;
		h2 = (key + 7438125) * 571723 % 999815601;
		if (h1 == h2) {
			h2 = ~h2;
			if (h1 == h2) {
				++h2;
			}
		}

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
		if (home_page->can_evict_foreign()) {
			home_page->evict_foreign(element, evicted);
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
				if (!insert_overflow_page(home_hash, evicted) || load_factor() >= load_factor_) {
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
	int32_t last_increment_;
	hash_function h1_;
};

#endif  // MHASHMAP_H_
