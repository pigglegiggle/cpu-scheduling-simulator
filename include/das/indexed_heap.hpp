#pragma once
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace das {
struct Key {
    std::int64_t rank = 0;
    std::uint64_t order = 0;
    bool operator==(const Key& b) const { return rank == b.rank && order == b.order; }
};
struct Entry { std::size_t id; Key key; };
inline bool before(const Entry& a, const Entry& b) {
    if (a.key.rank != b.key.rank) return a.key.rank < b.key.rank;
    if (a.key.order != b.key.order) return a.key.order < b.key.order;
    return a.id < b.id;
}
struct HeapStats { std::uint64_t comparisons=0, swaps=0, inserts=0, updates=0, erases=0; std::size_t peak=0; };

// The map contains exactly one live position for every heap entry: no tombstones.
class IndexedMinHeap {
    std::vector<Entry> heap_;
    std::unordered_map<std::size_t, std::size_t> positions_;
    HeapStats stats_;
    bool less(std::size_t a, std::size_t b) { ++stats_.comparisons; return before(heap_[a], heap_[b]); }
    void exchange(std::size_t a, std::size_t b) {
        std::swap(heap_[a], heap_[b]);
        positions_.at(heap_[a].id)=a; positions_.at(heap_[b].id)=b; ++stats_.swaps;
    }
    std::size_t up(std::size_t i) {
        while (i && less(i,(i-1)/2)) { auto p=(i-1)/2; exchange(i,p); i=p; }
        return i;
    }
    void down(std::size_t i) {
        for (;;) {
            auto child=2*i+1;
            if (child>=heap_.size()) break;
            if (child+1<heap_.size() && less(child+1,child)) ++child;
            if (!less(child,i)) break;
            exchange(i,child); i=child;
        }
    }
public:
    explicit IndexedMinHeap(std::size_t capacity=0) { heap_.reserve(capacity); positions_.reserve(capacity); }
    bool empty() const { return heap_.empty(); }
    std::size_t size() const { return heap_.size(); }
    bool contains(std::size_t id) const { return positions_.count(id)!=0; }
    const Entry& top() const { if (empty()) throw std::out_of_range("heap is empty"); return heap_.front(); }
    const HeapStats& stats() const { return stats_; }
    void insert(std::size_t id, Key key) {
        if (contains(id)) throw std::invalid_argument("duplicate heap ID");
        const auto index=heap_.size();
        heap_.push_back({id,key});
        try { positions_.emplace(id,index); } catch (...) { heap_.pop_back(); throw; }
        up(index); ++stats_.inserts;
        if (size()>stats_.peak) stats_.peak=size();
    }
    void update(std::size_t id, Key key) {
        auto i=positions_.at(id); heap_[i].key=key;
        down(up(i)); ++stats_.updates;
    }
    bool erase(std::size_t id) {
        auto found=positions_.find(id); if (found==positions_.end()) return false;
        auto i=found->second, last=heap_.size()-1;
        if (i!=last) exchange(i,last);
        heap_.pop_back(); positions_.erase(id); ++stats_.erases;
        if (i<heap_.size()) down(up(i));
        return true;
    }
    Entry pop() { auto result=top(); erase(result.id); return result; }
    const std::vector<Entry>& entries() const { return heap_; }
    bool valid() const {
        if (positions_.size()!=heap_.size()) return false;
        for (std::size_t i=0;i<heap_.size();++i) {
            auto pos=positions_.find(heap_[i].id);
            if (pos==positions_.end() || pos->second!=i) return false;
            if (i && before(heap_[i],heap_[(i-1)/2])) return false;
        }
        return true;
    }
};
}
