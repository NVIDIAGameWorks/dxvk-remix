#include <map>

namespace dxvk {
  template<typename T>
  class MemoryRegionMemoizer {
  private:
    struct Range {
      size_t start = 0;
      size_t end = 0;
      Range() = default;
      Range(size_t s, size_t e) : start(s), end(e) { }

      bool contains(size_t point) const {
        return start <= point && point < end;
      }

      bool overlaps(const Range& other) const {
        return (start <= other.start && other.start < end) ||
          (start < other.end&& other.end <= end) ||
          (other.start <= start && end <= other.end);
      }
    };

    template<typename U>
    struct CacheEntry {
      Range range = {};
      U result;
      CacheEntry() = default;
      CacheEntry(Range r, U res) : range(r), result(std::move(res)) { }
    };

    std::map<size_t, CacheEntry<T>> cache;

  public:
    template<typename Func>
    T memoize(size_t start, size_t size, Func&& func) {
      const Range currentRange(start, start + size);

      auto it = cache.find(start);
      if (it != cache.end() && it->second.range.start == currentRange.start && it->second.range.end == currentRange.end) {
        // Exact match found, return cached result
        return it->second.result;
      }

      // No exact match, invalidate all overlapping ranges
      invalidate(currentRange.start, currentRange.end);

      // If we didn't find a usable cached result, compute and store the result
      T result = std::invoke(func, start, size);
      cache[start] = CacheEntry<T>(currentRange, result);
      return result;
    }

    void invalidate(size_t start, size_t size) {
      Range invalidRange(start, start + size);

      // Find the first range overlapping range in cache
      auto it = cache.lower_bound(start);
      if (it != cache.begin()) --it;

      // Erase overlapping ranges
      while (it != cache.end() && it->second.range.start < invalidRange.end) {
        if (it->second.range.overlaps(invalidRange)) {
          it = cache.erase(it);
        } else {
          ++it;
        }
      }
    }

    void invalidateAll() {
      cache.clear();
    }
  };
}