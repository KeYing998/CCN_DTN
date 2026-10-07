// Implement an instrumented LRU policy that counts business cache operations and excludes control Data.
#pragma once
// Ordinary LRU semantics, with hooks for actual insertion/refresh/eviction.
// It derives from Policy: NFD's built-in LruPolicy is final.
#include "ns3/ndnSIM/NFD/daemon/table/cs.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/cs-policy.hpp"
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <stdexcept>
#include <string>

namespace dtn {
class MeasuredLru : public nfd::cs::Policy {
public:
  using Hook = std::function<void(const std::string&)>;
  MeasuredLru() : Policy("dtn-measured-lru") {}
  Hook inserted, refreshed, evicted;
private:
  using Queue = std::list<EntryRef>;
  // The front is least recently used; the back is most recently used.
  Queue queue;
  std::map<std::string, Queue::iterator> index;
  std::string Key(EntryRef entry) const { return entry->getFullName().toUri(); }
  void Touch(EntryRef entry) {
    auto it = index.find(Key(entry));
    if (it == index.end()) throw std::logic_error("LRU entry is not indexed");
    queue.splice(queue.end(), queue, it->second);
  }
  void doAfterInsert(EntryRef entry) override {
    // Remove control replies immediately without counting them as business cache insertions.
    if(entry->getName().toUri().find("/dtn/")==0){this->emitSignal(beforeEvict,entry);return;}
    queue.push_back(entry);
    index[Key(entry)] = std::prev(queue.end());
    if (inserted) inserted(entry->getName().toUri());
    evictEntries();
  }
  void doAfterRefresh(EntryRef entry) override {
    Touch(entry);
    if (refreshed) refreshed(entry->getName().toUri());
  }
  void doBeforeUse(EntryRef entry) override { Touch(entry); }
  void doBeforeErase(EntryRef entry) override {
    auto it = index.find(Key(entry));
    if (it != index.end()) { queue.erase(it->second); index.erase(it); }
  }
  // Enforce the quota after insertion or capacity reduction.
  void evictEntries() override {
    while (getCs()->size() > getLimit()) {
      if (queue.empty()) throw std::logic_error("LRU queue unexpectedly empty");
      EntryRef entry = queue.front();
      const std::string name = entry->getName().toUri();
      index.erase(Key(entry));
      queue.pop_front();
      if (evicted) evicted(name);
      this->emitSignal(beforeEvict, entry);
    }
  }
};
} // namespace dtn
