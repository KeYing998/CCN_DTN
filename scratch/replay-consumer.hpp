// Send a predetermined request sequence and record completion or timeout for each logical request.
#pragma once
#include "workload.hpp"
#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/ndnSIM/model/ndn-app-link-service.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <string>

namespace dtn { using namespace ns3; }

namespace dtn {
class ReplayConsumer : public ns3::ndn::App {
public:
  static TypeId GetTypeId() {
    static TypeId tid=TypeId("dtn::ReplayConsumer").SetParent<ns3::ndn::App>()
      .AddConstructor<ReplayConsumer>()
      .AddTraceSource("FirstInterestDataDelay","Delay from original app request to Data",
        MakeTraceSourceAccessor(&ReplayConsumer::firstDelay),
        "ns3::ndn::Consumer::FirstInterestDataDelayCallback")
      .AddTraceSource("LastRetransmittedInterestDataDelay","Delay from last send to Data",
        MakeTraceSourceAccessor(&ReplayConsumer::lastDelay),
        "ns3::ndn::Consumer::LastRetransmittedInterestDataDelayCallback");
    return tid;
  }
  std::vector<Request> requests;
  uint64_t sent=0, received=0, timeouts=0;
  double delaySum=0;
  std::function<void(unsigned,uint64_t,const std::string&,double,double)> result;
  unsigned consumer=0;
  void OnData(std::shared_ptr<const ::ndn::Data> data) override {
    ns3::ndn::App::OnData(data);
    auto name=data->getName().toUri();
    auto it=byName.find(name);
    if (it==byName.end()) return;
    double now=Simulator::Now().GetSeconds();
    // A single Data can satisfy multiple outstanding app requests for a name.
    for (auto id : it->second) {
      auto p=pending.find(id);
      if (p==pending.end()) continue;
      ++received;
       delaySum+=now-p->second;
      const Time delay=Seconds(now-p->second);
      // No retransmissions in this app: one transmission per logical request.
      // Hop count is unavailable here and explicitly reported as -1.
      firstDelay(this,static_cast<uint32_t>(id),delay,1,-1);
      lastDelay(this,static_cast<uint32_t>(id),delay,-1);
      if (result) result(consumer,id,name,p->second,now);
      pending.erase(p);
    }
    byName.erase(it);
  }
private:
  TracedCallback<Ptr<ns3::ndn::App>,uint32_t,Time,uint32_t,int32_t> firstDelay;
  TracedCallback<Ptr<ns3::ndn::App>,uint32_t,Time,int32_t> lastDelay;
  // Track IDs separately because multiple requests can share a content name.
  std::map<uint64_t,double> pending;
  std::map<std::string,std::vector<uint64_t>> byName;
  void StartApplication() override {
    ns3::ndn::App::StartApplication();
    for (size_t i=0;i<requests.size();++i)
      Simulator::Schedule(Seconds(requests[i].time),&ReplayConsumer::Send,this,i);
  }
  void StopApplication() override { ns3::ndn::App::StopApplication();
   }
  void Send(size_t id) {
    if (!m_active) return;
    auto interest=std::make_shared<::ndn::Interest>(::ndn::Name("/catalog/"+std::to_string(requests[id].content)));
    // Deterministic per-request nonce; same workload in every experiment mode.
    interest->setNonce(static_cast<uint32_t>((id+1)*2654435761u + consumer*101u));
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(::ndn::time::milliseconds(2000));
    const std::string name=interest->getName().toUri();
    pending[id]=Simulator::Now().GetSeconds();
     byName[name].push_back(id);
     ++sent;
    m_transmittedInterests(interest,this,m_face);
    m_appLink->onReceiveInterest(*interest);
    Simulator::Schedule(Seconds(2),&ReplayConsumer::Timeout,this,id,name);
  }
  // A removed pending ID has already completed and must not be counted as a timeout.
  void Timeout(uint64_t id, std::string name) {
    auto p=pending.find(id);
    if (p==pending.end()) return;
    double issued=p->second;
     pending.erase(p);
     ++timeouts;
    auto it=byName.find(name);
    if (it!=byName.end()) {
      auto& ids=it->second;
       ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
      if (ids.empty()) byName.erase(it);
    }
    if (result) result(consumer,id,name,issued,-1);
  }
};
NS_OBJECT_ENSURE_REGISTERED(ReplayConsumer);
} // namespace dtn
