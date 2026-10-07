// Model topology paths, replay joint LRU caches, compare quota candidates, and plan controller updates.
#pragma once
#include "dtn-core.hpp"
#include <algorithm>
#include <deque>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dtn {
struct Edge {std::string a,b,rate,delay; unsigned metric=1,queue=100;};
struct Topology {
  std::map<std::string,std::string> roles;
  std::vector<Edge> edges;
  // The scenario supplies graph data extracted from AnnotatedTopologyReader.
  // This core never parses topology TXT or creates network objects.
  static Topology FromGraph(const std::map<std::string,std::string>& roles,
                            const std::vector<Edge>& edges) {
    Topology t;t.roles=roles;t.edges=edges;
    const std::map<std::string,std::string> expected{{"C1","consumer"},{"C2","consumer"},{"R1","router"},{"R2","router"},{"R3","router"},{"P","producer"},{"D","digital"}};
    if(t.roles!=expected)throw std::runtime_error("v2 requires C1,C2,R1,R2,R3,P,D with documented roles");
    std::set<std::pair<std::string,std::string>> seen;std::map<std::string,unsigned> degree;
    for(const auto&e:t.edges){if(e.a==e.b||!e.metric)throw std::runtime_error("invalid graph edge");
      if(!t.roles.count(e.a)||!t.roles.count(e.b))throw std::runtime_error("unknown link endpoint");
      auto key=std::minmax(e.a,e.b);if(!seen.emplace(key.first,key.second).second)throw std::runtime_error("duplicate link");++degree[e.a];++degree[e.b];}
    for(auto name:{"C1","C2","D","P"})if(degree[name]!=1)throw std::runtime_error("C1,C2,D,P must be leaves in v2");
    for(const auto&kv:t.roles)t.Path(kv.first,"P");
    if(t.Path("C1","P").at(1)!="R1"||t.Path("C2","P").at(1)!="R2")throw std::runtime_error("C1 must attach to R1 and C2 to R2");
    return t;
  }
  std::vector<std::string> Path(const std::string& source,const std::string& dest) const {
    if(!roles.count(source)||!roles.count(dest))throw std::runtime_error("unknown path endpoint");
    // Dijkstra search uses positive link metrics to derive a single upstream path.
    std::map<std::string,double> dist;std::map<std::string,std::string> prev;std::set<std::string> visited;
    for(const auto&kv:roles)dist[kv.first]=std::numeric_limits<double>::infinity();
    dist[source]=0;
    while(visited.size()<roles.size()) {
      std::string u;double best=std::numeric_limits<double>::infinity();
      for(const auto&kv:dist)if(!visited.count(kv.first)&&kv.second<best){u=kv.first;best=kv.second;}
      if(u.empty())break;
      visited.insert(u);if(u==dest)break;
      for(const auto&e:edges){std::string v=e.a==u?e.b:e.b==u?e.a:"";if(v.empty())continue;
        if(dist[v]>best+e.metric){dist[v]=best+e.metric;prev[v]=u;}}
    }
    if(!std::isfinite(dist[dest]))throw std::runtime_error("disconnected topology");
    std::vector<std::string> path;for(std::string u=dest;;u=prev.at(u)){path.push_back(u);if(u==source)break;}
    std::reverse(path.begin(),path.end());return path;
  }
  std::vector<unsigned> RouterPath(unsigned ingress) const {
    auto p=Path("R"+std::to_string(ingress+1),"P");std::vector<unsigned> out;
    for(const auto&n:p)if(n.size()==2&&n[0]=='R')out.push_back(unsigned(n[1]-'1'));
    return out;
  }
};
struct Event {double time=0;unsigned ingress=0;std::string content;};
struct TwinSnapshot {
  double sampled=0,interval=0,lastChange=0;unsigned capacity=0,occupancy=0;uint64_t version=0;
  uint64_t requests=0,hits=0,misses=0,dropped=0;
  std::vector<Event> events;
  std::string Encode() const {
    std::ostringstream s;s.precision(17);
    s<<sampled<<' '<<interval<<' '<<capacity<<' '<<occupancy<<' '<<version<<' '<<requests<<' '<<hits<<' '<<misses<<' '<<dropped<<' '<<lastChange<<'\n';
    for(const auto&e:events)s<<e.time<<' '<<e.ingress<<' '<<e.content<<'\n';
    return s.str();
  }
  static TwinSnapshot Decode(const std::string& text) {
    TwinSnapshot b;std::istringstream s(text);
    if(!(s>>b.sampled>>b.interval>>b.capacity>>b.occupancy>>b.version>>b.requests>>b.hits>>b.misses>>b.dropped>>b.lastChange)||
       !std::isfinite(b.sampled)||!std::isfinite(b.interval)||b.interval<=0||b.occupancy>b.capacity)
      throw std::runtime_error("invalid telemetry header");
    Event e;while(s>>e.time>>e.ingress>>e.content){
      if(!std::isfinite(e.time)||e.time>b.sampled||e.ingress>1||e.content.find("/catalog/")!=0)throw std::runtime_error("invalid telemetry event");
      b.events.push_back(e);if(b.events.size()>8192)throw std::runtime_error("oversized telemetry");}
    if(!s.eof())throw std::runtime_error("malformed telemetry");
    return b;
  }
};
class ShadowLru {
  unsigned capacity;std::list<std::string> lru;std::map<std::string,std::list<std::string>::iterator> index;
public:
  explicit ShadowLru(unsigned c):capacity(c){}
  bool Has(const std::string& key){auto i=index.find(key);if(i==index.end())return false;lru.splice(lru.end(),lru,i->second);return true;}
  void Insert(const std::string& key){if(Has(key)||!capacity)return;lru.push_back(key);index[key]=std::prev(lru.end());
    while(lru.size()>capacity){index.erase(lru.front());lru.pop_front();}}
};
struct Estimate {double cost=0;uint64_t requests=0;std::array<uint64_t,3>hits{{0,0,0}},lookups{{0,0,0}};};
// Joint replay propagates each miss upstream. No independent hit-rate summation.
inline Estimate Replay(const std::vector<Event>& events,const Quotas&q,const Topology&t,double scoreAfter) {
  // Start with empty virtual caches; earlier events warm them without contributing to the score.
  std::array<ShadowLru,3> cache{{ShadowLru(q[0]),ShadowLru(q[1]),ShadowLru(q[2])}};
  std::array<std::vector<unsigned>,2> paths{{t.RouterPath(0),t.RouterPath(1)}};Estimate out;
  for(const auto&e:events){const auto&path=paths.at(e.ingress);std::vector<unsigned> traversed;unsigned hops=0;
    const bool scored=e.time>=scoreAfter;
    for(unsigned r:path){traversed.push_back(r);if(scored)++out.lookups[r];
      if(cache[r].Has(e.content)){if(scored)++out.hits[r];break;}++hops;}
    // Returning Data fills traversed caches; this replay omits network timing and PIT aggregation.
    for(auto r:traversed)cache[r].Insert(e.content);
    if(scored){++out.requests;out.cost+=hops;}}
  return out;
}
struct Choice {Quotas quota;double before=0,after=0;unsigned evaluated=0;uint64_t replayEvents=0;};
inline Choice Choose(const std::vector<Event>&events,const Quotas&current,const Quotas&heuristic,
                     const Topology&t,unsigned lower,unsigned upper,unsigned step,double threshold) {
  // Keep the current allocation until at least twenty historical events are available.
  Choice c;c.quota=current;if(events.size()<20)return c;
  const double cutoff=events.front().time+(events.back().time-events.front().time)*0.5;
  // Compare current and heuristic quotas with feasible one-step transfers, not a global search.
  std::set<Quotas> candidates{current,heuristic};
  for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)if(a!=b&&current[a]>=lower+step&&current[b]+step<=upper){auto q=current;q[a]-=step;q[b]+=step;candidates.insert(q);}
  c.before=Replay(events,current,t,cutoff).cost;c.after=c.before;c.replayEvents=events.size();
  for(const auto&q:candidates){double cost=Replay(events,q,t,cutoff).cost;++c.evaluated;if(cost<c.after){c.after=cost;c.quota=q;}}
  // Require a relative hop-cost improvement before replacing the current allocation.
  if(c.before<=0||(c.before-c.after)/c.before<threshold){c.quota=current;c.after=c.before;}return c;
}
inline double NextPeriod(double current,double change,unsigned stable,double minimum,double maximum) {
  // Large changes reset the period to its minimum; three stable rounds permit doubling.
  if(change>0.25)return minimum;
  if(stable>=3)return std::min(maximum,current*2);
  return current;
}
inline std::vector<std::pair<unsigned,unsigned>> PlanChanges(const Quotas& current,const Quotas& target) {
  std::vector<std::pair<unsigned,unsigned>> result;
  // Release entries before expansion so intermediate allocations remain within budget.
  for(unsigned k=0;k<3;++k)if(target[k]<current[k])result.emplace_back(k,target[k]);
  for(unsigned k=0;k<3;++k)if(target[k]>current[k])result.emplace_back(k,target[k]);
  return result;
}
} // namespace dtn
