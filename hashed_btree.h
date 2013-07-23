#ifndef HASHED_BTREE_H_
#define HASHED_BTREE_H_

#include <algorithm>
#include <cstdint>

#define CACHELINE_SIZE 128

struct btree_page;
struct hash_page;

struct page {
	enum {
		enum_hash_page = 0,
		enum_btree_page = 1,
	};
	typedef uint64_t key_t;
	typedef uint64_t value_t;
	typedef std::pair<key_t, value_t> elem_t;
	uint8_t tag_ : 1;
	uint8_t size_ : 7;

	page() : tag_(0), size_(0) {}

	btree_page* get_btree();
	hash_page* get_hash();
};

struct btree_page : public page {
	struct extent {
		typedef uint64_t key_t;
		typedef uint64_t value_t;
		static const int kMaxItem = 8;
		elem_t item_[kMaxItem];

		elem_t* find(const key_t& k, int size) {
			for (int i = 0; i < size; ++i) {
				if (item_[i].first == k) {
					return &item_[i];
				}
			}
			return nullptr;
		}

		void insert_at(int pos, elem_t&& elem) {
			item_[pos] = std::move(elem);
		}

		void sort(int size) {
			std::sort(&item_[0], &item_[size]);
		}

		int split_half(extent* npage, int size) {
			int num_copy = size / 2;
			for (int i = num_copy, j = 0; j < num_copy; ++i, ++j) {
				npage->item_[j] = std::move(item_[i]);
			}
			return num_copy;
		}
	};

	static const int kMaxKey = 6;
	uint8_t child_size_[kMaxKey + 1];
	extent* link_[kMaxKey + 1];
	key_t key_[kMaxKey];
	uint8_t padding__[16];

	void release() {
		for (int i = 0; i < size_ + 1; ++i) {
			delete link_[i];
		}
	}

	bool is_child_full(int i) const {
		return child_size_[i] >= extent::kMaxItem;
	}

	bool is_full_key() const {
		return size_ >= kMaxKey;
	}

	void split_child(int target) {
		link_[target]->sort(child_size_[target]);
		extent* npage = new extent;
		int num_copied = link_[target]->split_half(npage, child_size_[target]);
		child_size_[target] -= num_copied;

		for (int i = size_; i > target; --i) {
			key_[i] = std::move(key_[i - 1]);
			link_[i + 1] = link_[i];
			child_size_[i + 1] = child_size_[i];
		}

		child_size_[target + 1] = num_copied;
		link_[target + 1] = npage;
		key_[target] = npage->item_[0].first;
		++size_;
	}

	bool try_insert_at_child(int i, elem_t&& elem) {
		if (is_child_full(i)) {
			if (is_full_key()) {
				return false;
			} else {
				split_child(i);
				if (elem.first < key_[i]) {
					return try_insert_at_child(i, std::forward<elem_t>(elem));
				} else {
					return try_insert_at_child(i + 1, std::forward<elem_t>(elem));
				}
			}
		} else {
			link_[i]->insert_at(child_size_[i], std::forward<elem_t>(elem));
			++child_size_[i];
			return true;
		}
	}

	bool insert(elem_t&& elem) {
		int i = 0;
		for (; i < size_; ++i) {
			if (elem.first < key_[i]) {
				break;
			}
		}
		return try_insert_at_child(i, std::forward<elem_t>(elem));
	}

	elem_t* find(const key_t& k) {
		int i = 0;
		for (; i < size_; ++i) {
			if (k < key_[i]) {
				break;
			}
		}
		return link_[i]->find(k, child_size_[i]);
	}

	size_t size() {
		size_t ret = 0;
		for (int i = 0; i < size_; ++i) {
			ret += child_size_[i];
		}
		return ret;
	}
};

struct hash_page : public page {
	static const int kMaxItem = 7;
	uint8_t flag_[kMaxItem];
	uint8_t padding__[8];
	elem_t item_[kMaxItem];

	bool is_full() const {
		return size_ >= kMaxItem;
	}

	bool insert(elem_t&& elem) {
		if (is_full()) {
			return false;
		}
		item_[size_] = std::move(elem);
		++size_;
		return true;
	}

	elem_t* find(const key_t& k) {
		for (int i = 0; i < size_; ++i) {
			if (item_[i].first == k) {
				return &item_[i];
			}
		}
		return nullptr;
	}

	size_t size() {
		return size_;
	}
};

inline btree_page* page::get_btree() { return static_cast<btree_page*>(this); }
inline hash_page* page::get_hash() { return static_cast<hash_page*>(this); }

class hashed_btree {
public:
	typedef uint64_t key_t;
	typedef uint64_t value_t;

	class iterator {
	public:
		iterator(page::elem_t* e) : e_(e) {}

		void next();

		page::elem_t& operator *() { return *e_; }
		const page::elem_t& operator *() const { return *e_; }

		page::elem_t* operator ->() { return e_; }
		const page::elem_t* operator ->() const { return e_; }

		bool operator==(const iterator& rhs) const { return e_ == rhs.e_; }
		bool operator!=(const iterator& rhs) const { return e_ != rhs.e_; }

	private:
		page::elem_t* e_;
	};
	
	static btree_page* hash_page_to_btree_page(hash_page* p) {
		btree_page::extent* npage = new btree_page::extent;
		int old_size = p->size_;
		for (int i = 0; i < old_size; ++i) {
			npage->insert_at(i, std::move(p->item_[i]));
		}

		btree_page* bpage = reinterpret_cast<btree_page*>(p);

		bpage->tag_ = page::enum_btree_page;
		bpage->size_ = 0;
		bpage->child_size_[0] = old_size;
		for (int i = 1; i < btree_page::kMaxKey + 1; ++i) {
			bpage->child_size_[i] = 0;
		}
		bpage->link_[0] = npage;
		return bpage;
	}

	static const int kDefaultCapacity = 1;

	hashed_btree() {
		init(kDefaultCapacity);
	}

	~hashed_btree() {
		for (uint32_t i = 0; i < capacity_; ++i) {
			if (get_page(i)->tag_ == page::enum_btree_page) {
				get_page(i)->get_btree()->release();
			}
		}
		free(page_);
	}

	size_t size() const { return size_; }

	void insert(page::elem_t&& e) {
		if (size_ * 1000 >= capacity_ * hash_page::kMaxItem * load_factor_) {
			resize();
		}
		int fail_count = 0;
		const int kMaxInsertFail = 5;
		while (fail_count < kMaxInsertFail) {
			page* p = get_page_by_hash(e.first);
			if (p->tag_ == page::enum_hash_page) {
				hash_page* hpage = p->get_hash();
				if (hpage->insert(std::forward<page::elem_t>(e))) {
					++size_;
					return;
				}
				hash_page_to_btree_page(hpage);
			}

			btree_page* bpage = p->get_btree();
			if (bpage->insert(std::forward<page::elem_t>(e))) {
				++size_;
				return;
			}

			resize();
			++fail_count;
		}
	}

	void resize() {
		double mega_capacity = static_cast<double>(size()) / num_page() / hash_page::kMaxItem;
		std::cout << "resizing : " << mega_capacity << " " << size() << std::endl;

		// TODO: implement inplace resizing.
		hashed_btree new_target(capacity_ * 2);
		for (uint32_t i = 0; i < capacity_; ++i) {
			page* p = get_page(i);
			if (p->tag_ == page::enum_hash_page) {
				hash_page* hpage = p->get_hash();
				for (int i = 0; i < hpage->size_; ++i) {
					new_target.insert(std::move(hpage->item_[i]));
				}
			} else {
				btree_page* bpage = p->get_btree();
				for (int i = 0; i < bpage->size_ + 1; ++i) {
					btree_page::extent* e = bpage->link_[i];
					for (int j = 0; j < bpage->child_size_[i]; ++j) {
						new_target.insert(std::move(e->item_[j]));
					}
				}
			}
		}
		std::swap(capacity_, new_target.capacity_);
		std::swap(size_, new_target.size_);
		std::swap(page_, new_target.page_);
	}

	iterator find(const key_t& k) const {
		page* p = get_page_by_hash(k);
		page::elem_t* ret;
		if (p->tag_ == page::enum_hash_page) {
			ret = p->get_hash()->find(k);
		} else {
			ret = p->get_btree()->find(k);
		}
		if (ret == nullptr) {
			return end();
		}
		return iterator(ret);
	}

	iterator end() const { return iterator(reinterpret_cast<page::elem_t*>(get_page(capacity_))); }

	size_t num_page() const { return capacity_; }

private:
	hashed_btree(uint32_t new_capacity) {
		init(new_capacity);
	}

	void init(uint32_t capacity) {
		capacity_ = capacity;
		size_ = 0;
		size_t alloc_size = sizeof(hash_page) * capacity_;
		page_ = reinterpret_cast<page*>(malloc(alloc_size));
		memset(page_, 0, alloc_size);
	}

	page* get_page_by_hash(const key_t& k) const {
		int h = hash_func_(k) % capacity_;
		return get_page(h);
	}

	page* get_page(int index) const {
		return reinterpret_cast<page*>(reinterpret_cast<uintptr_t>(page_) + index * CACHELINE_SIZE);
	}

	uint32_t capacity_;
	uint32_t size_; 
	static const uint64_t load_factor_ = 900;
	std::hash<key_t> hash_func_;
	page* page_;
	// TODO: implement linear array and realloc-able extent.
	//extent* btree_;
};

#endif  // HASHED_BTREE_H_
