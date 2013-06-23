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

#include <xmmintrin.h>

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

struct doublebitmap_t {
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
	uint16_t bm;
};

struct mhashpage {
	static const int kMaxLevel = 3;
	static const int kMaxElements = 8;
	typedef uint64_t key_t;
	typedef uint64_t value_t;
	typedef std::pair<key_t, value_t> entry_t;
	struct context {
		uint16_t num_elements;
		uint16_t foreign_placed[kMaxLevel];
		uint8_t flags[kMaxElements];
	} cxt;
	entry_t entries[(HASHPAGE_SIZE - sizeof(context)) / sizeof(entry_t)];
	static const int num_max_entries = (HASHPAGE_SIZE - sizeof(context)) / sizeof(entry_t);

	bool overflow(int level) const {
		return cxt.foreign_placed[level] != 0;
	}

	bool full() const {
		return cxt.num_elements == num_max_entries;
	}

	bool empty() const {
		return cxt.num_elements == 0;
	}

	entry_t* find(const key_t& k) {
		for (int i = 0; i < cxt.num_elements; ++i) {
			if (entries[i].first == k) {
				return &entries[i];
			}
		}
		return nullptr;
	}

	bool try_evict_foreign(entry_t& evicted, int& evicted_level, int minimum_evict_level) {
		int target = -1;
		for (int i = 0; i < cxt.num_elements; ++i) {
			if (cxt.flags[i] > minimum_evict_level) {
				minimum_evict_level = cxt.flags[i];
				target = i;
			}
		}
		if (target != -1) {
			std::swap(entries[target], evicted);
			cxt.flags[target] = evicted_level;
			evicted_level = minimum_evict_level;
			return true;
		}
		return false;
	}

	bool insert(const entry_t& element, int level) {
		if (full()) {
			return false;
		}
		entries[cxt.num_elements] = element;
		cxt.flags[cxt.num_elements] = level;
		++cxt.num_elements;
		return true;
	}

	void erase(int index) {
		--cxt.num_elements;
		if (cxt.num_elements == 0 || index == cxt.num_elements) {
			return;
		}
		entries[index] = entries[cxt.num_elements];
		cxt.flags[index] =cxt.flags[cxt.num_elements];
	}
};

// TODO: STL conformity.
// 8 byte key and 8 byte value
class mhashmap {
public:
	typedef uint64_t key_t;
	typedef uint64_t value_t;
	static const int kMaxPlacementStatus = mhashpage::kMaxLevel + 1;
	//typedef uint32_t hash_array_t[kMaxPlacementStatus];
	typedef __m128i hash_array_t;
	class iterator {
	public:
		iterator(mhashpage::entry_t* e) : e_(e) {}

		void next();

		mhashpage::entry_t& operator *() { return *e_; }
		const mhashpage::entry_t& operator *() const { return *e_; }

		mhashpage::entry_t* operator ->() { return e_; }
		const mhashpage::entry_t* operator ->() const { return e_; }

		bool operator==(const iterator& rhs) const { return e_ == rhs.e_; }
		bool operator!=(const iterator& rhs) const { return e_ != rhs.e_; }

	private:
		mhashpage::entry_t* e_;
	};

	mhashmap() {
		const int kInitialCapacity = 2;
		init(kInitialCapacity);
	}

	mhashmap(int32_t capacity) {
		init(capacity);
	}

	~mhashmap() {
		free(page_);
	}

#define GET(k, x) ((reinterpret_cast<uint32_t*>(&k))[x])

	size_t overflow_rate(int level) const {
		size_t num_overflow = 0;
		for (mhashpage* iter = page_; iter < page_ + capacity_; ++iter) {
			if (iter->overflow(level)) {
				++num_overflow;
			}
		}
		return num_overflow;
	}

	size_t overflow_rate() const {
		size_t num_overflow = 0;
		for (mhashpage* iter = page_; iter < page_ + capacity_; ++iter) {
			for (int i = 0; i < mhashpage::kMaxLevel; ++i) {
				if (iter->overflow(i)) {
					num_overflow += i + 1;
				}
			}
		}
		return num_overflow;
	}

	size_t capacity() const { return capacity_ * mhashpage::num_max_entries; }
	size_t size() const { return num_entries_; }

	void debug_find(int idx) {
		for (int i = 0; i < capacity_; ++i) {
			for (int j = 0; j < mhashpage::num_max_entries; ++j) {
				if (page_[i].entries[j].first == idx) {
					page_[i].entries[j].first = idx;
				}
			}
		}
	}

	void set_capacity_mask() {
		uint32_t mask = capacity_ - 1;
		float* f_mask = reinterpret_cast<float*>(&mask);
		__m128 m = _mm_set_ps1(*f_mask);
		capacity_mask_ = *reinterpret_cast<__m128i*>(&m);
	}

	int load_factor() const {
		return num_entries_ * 1000LL / mhashpage::num_max_entries / (capacity_ + num_overflow_page_);
	}

	void rebuild_or_rehash() {
		uint64_t current_load = load_factor();
		//std::cout << "current load : " << current_load << " capacity : " << capacity_ << std::endl;
		if (current_load > load_factor_) {
			rebuild();
		} else {
			//std::cout << "rehash" << std::endl;
			rehash();
		}
	}

	bool rebuild_cuckoo(int i, int j) {
		hash_array_t key_hash;
		compute_hash(page_[i].entries[j].first, key_hash);

		for (int l = 0; l < kMaxPlacementStatus; ++l) {
			if (GET(key_hash, l) == i) {
				for (int j = 0; j < l; ++j) {
					++page_[GET(key_hash, j)].cxt.foreign_placed[j];
				}
				return true;
			}
		}

		mhashpage::entry_t evicted = page_[i].entries[j];
		page_[i].erase(j);
		insert_internal(evicted, key_hash);
		return false;
	}


	void increment_capacity() {
		capacity_ *= 2;
	}

	void rebuild() {
		int32_t old_capacity = capacity_;

		increment_capacity();
		while (num_entries_ >= capacity_ * mhashpage::num_max_entries) {
			increment_capacity();
		}

		page_ = reinterpret_cast<mhashpage*>(realloc(page_, sizeof(mhashpage) * capacity_));
		std::memset(&page_[old_capacity], 0, sizeof(mhashpage) * (capacity_ - old_capacity));
		set_capacity_mask();

		for (int i = 0; i < old_capacity; ++i) {
			for (int j = 0; j < mhashpage::kMaxLevel; ++j) {
				page_[i].cxt.foreign_placed[j] = 0;
			}
		}

		for (int i = 0; i < old_capacity; ++i) {
			if (page_[i].empty()) {
				continue;
			}
			for (int j = 0; j < page_[i].cxt.num_elements; ) {
				if (rebuild_cuckoo(i, j)) {
					++j;
				}
			}
		}
	}

	void rehash() {
		// TODO: implement rehash correctly.
		rebuild();
	}

	void compute_hash(const key_t& key, hash_array_t& h) {
		uint32_t k = key;
		float* f_k = reinterpret_cast<float*>(&k);
		__m128 m = _mm_set_ps1(*f_k);
		h = *reinterpret_cast<__m128i*>(&m);
		h = _mm_add_epi32(h, hash_add_);
		h = _mm_mullo_epi32(h, hash_mult_);
		h = _mm_and_si128(h, capacity_mask_);

		//h[0] = static_cast<uint32_t>(h1_(key, 0));
		//h[1] = (key + 7438125) * 571723 % 999815601;
		//h[2] = (key + 192387) * 1298044988 % 381782731;
		//h[3] = (key + 47741) * 48483 % 18283747522222;
		////hashlittle2(&key, sizeof(key_t), &h[2], &h[3]);

		//if (h[0] == h[1]) {
		//	++h[1];
		//}
		//if (h[1] == h[2]) {
		//	++h[2];
		//}
		//if (h[2] == h[3]) {
		//	++h[3];
		//}

		//h[0] %= capacity_;
		//h[1] %= capacity_;
		//h[2] %= capacity_;
		//h[3] %= capacity_;

	}


	mhashpage::entry_t* find_internal(const key_t& k, hash_array_t& key_hash) {
		mhashpage::entry_t* entry;
		for (int i = 0; i < kMaxPlacementStatus; ++i) {
			if ((entry = page_[GET(key_hash, i)].find(k)) != nullptr) {
				return entry;
			}
			if (i != mhashpage::kMaxLevel && !page_[GET(key_hash, i)].overflow(i)) {
				break;
			}
		}
		return nullptr;
	}

	void increase_foreign_element(int level, hash_array_t& key_hash) {
		for (int i = 0; i < level; ++i) {
			++page_[GET(key_hash, i)].cxt.foreign_placed[i];
		}
	}

	void decrease_foreign_element(int level, hash_array_t& key_hash) {
		for (int i = 0; i < level; ++i) {
			--page_[GET(key_hash, i)].cxt.foreign_placed[i];
		}
	}

	bool try_insert(const mhashpage::entry_t& element, hash_array_t key_hash) {
		for (int i = 0; i < kMaxPlacementStatus; ++i) {
			if (page_[GET(key_hash, i)].insert(element, i)) {
				increase_foreign_element(i, key_hash);
				return true;
			}
		}
		return false;
	}

	bool try_evict_foreign(mhashpage::entry_t& evicted, int& last_evicted_level, hash_array_t key_hash) {
		for (int i = 0; i < kMaxPlacementStatus; ++i) {
			if (last_evicted_level != i) {
				int evict_level = i;
				if (page_[GET(key_hash, i)].try_evict_foreign(evicted, evict_level, i)) {
					last_evicted_level = evict_level;
					return true;
				}
			}
		}
		return false;
	}

	void evict_any(mhashpage::entry_t& evicted, int& last_evicted_level, hash_array_t key_hash) {
		int evict_level = 0;
		bool ret = page_[GET(key_hash, 0)].try_evict_foreign(evicted, evict_level, -1);
#ifdef _DEBUG
		assert(ret == true);
#endif
		last_evicted_level = evict_level;
	}

	void insert_internal(const mhashpage::entry_t& element, hash_array_t key_hash) {
		if (try_insert(element, key_hash)) {
			return;
		}

		mhashpage::entry_t evicted = element;
		int last_evicted_level = -1;

		while (true) {
			int count = 0;
			while (count < MAX_ITERATION) {
				if (!try_evict_foreign(evicted, last_evicted_level, key_hash)) {
					evict_any(evicted, last_evicted_level, key_hash);
				}
				compute_hash(evicted.first, key_hash);
				decrease_foreign_element(last_evicted_level, key_hash);
				// TODO: implement try_insert_except.
				if (try_insert(evicted, key_hash)) {
					return;
				}
				++count;
			}
			rebuild_or_rehash();
			compute_hash(evicted.first, key_hash);
			if (try_insert(evicted, key_hash)) {
				return;
			}
		}
	}

	void insert(const mhashpage::entry_t& element) {

		//if (load_factor() > load_factor_) {
		//	rebuild_or_rehash();
		//}

		hash_array_t key_hash;
		compute_hash(element.first, key_hash);

		mhashpage::entry_t* entry = find_internal(element.first, key_hash);
		if (entry != nullptr) {
			// update the entry
			return;
		}
		insert_internal(element, key_hash);
		++num_entries_;
	}


	iterator find(const key_t& k) {
		hash_array_t key_hash;
		compute_hash(k, key_hash);
		mhashpage::entry_t* e = find_internal(k, key_hash);
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
	static const int MAX_ITERATION = 10;

	// 70% occupancy
	static const uint32_t load_factor_ = 700;

	void init(int32_t capacity) {
		page_ = reinterpret_cast<mhashpage*>(malloc(sizeof(mhashpage) * capacity));
		capacity_ = capacity;
		num_entries_ = 0;
		num_overflow_page_ = 0;
		std::memset(page_, 0, sizeof(mhashpage) * capacity);
		set_capacity_mask();

		static const uint32_t addv[4] = {1923775UL, 47472UL, 575757172UL, 39192381UL};
		hash_add_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(addv)); 
		static const uint32_t multv[4] = {512775UL, 47471093UL, 6761UL, 83192381UL};
		hash_mult_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(multv));
	}

	mhashpage* page_;
	int32_t num_entries_;
	int32_t capacity_;
	int32_t num_overflow_page_;
	//hash_function h1_;
	hash_array_t capacity_mask_;
	hash_array_t hash_add_;
	hash_array_t hash_mult_;
};

#endif  // MHASHMAP_H_
