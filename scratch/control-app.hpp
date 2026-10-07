// Exchange controller requests and router replies through NDN Interest and Data packets.
#pragma once
#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/ndnSIM/model/ndn-app-link-service.hpp"
#include <functional>
#include <stdexcept>
#include <string>

namespace dtn {
// Transport only. PO state is read by serve; LO state is updated ONLY by Data.
class ControlApp : public ns3::ndn::App {
public:
  static ns3::TypeId GetTypeId(){static ns3::TypeId id=ns3::TypeId("dtn::ControlApp").SetParent<ns3::ndn::App>().AddConstructor<ControlApp>();
  return id;
  }
  std::string prefix;
  std::function<std::string(const ::ndn::Name&)> serve;
  std::function<void(const std::string&,const std::string&)> delivered;
  void Send(const std::string& name,unsigned nonce){
    if(!m_active)return;
    auto p=std::make_shared<::ndn::Interest>(::ndn::Name(name));
    p->setNonce(nonce);
    // Match the exact RPC name so each reply belongs to one query or command.
    p->setCanBePrefix(false);
    p->setMustBeFresh(true);
    p->setInterestLifetime(::ndn::time::milliseconds(500));
    m_transmittedInterests(p,this,m_face);
    m_appLink->onReceiveInterest(*p);
  }
  void OnInterest(std::shared_ptr<const ::ndn::Interest> p) override {
    ns3::ndn::App::OnInterest(p);
    if(!serve)return;
    std::string content=serve(p->getName());
    auto data=std::make_shared<::ndn::Data>(p->getName());
    // ndnSIM 2.8/2.9 matchesData rejects zero freshness for MustBeFresh.
    // MeasuredLru excludes /dtn/ Data from CS; freshness is not a no-cache flag.
    data->setFreshnessPeriod(::ndn::time::milliseconds(1000));
    data->setContent(reinterpret_cast<const uint8_t*>(content.data()),content.size());
    ns3::ndn::StackHelper::getKeyChain().sign(*data);
    if(!p->matchesData(*data))
      throw std::runtime_error("Control reply does not match Interest: "+p->getName().toUri());
    m_transmittedDatas(data,this,m_face);
    m_appLink->onReceiveData(*data);
  }
  void OnData(std::shared_ptr<const ::ndn::Data> p) override {
    ns3::ndn::App::OnData(p);
    // Pass reply payloads to the controller for RPC state and version checks.
    const auto& b=p->getContent();
    if(delivered)delivered(p->getName().toUri(),std::string(reinterpret_cast<const char*>(b.value()),b.value_size()));
  }
private:
  void StartApplication() override {ns3::ndn::App::StartApplication();
  if(!prefix.empty())ns3::ndn::FibHelper::AddRoute(GetNode(),::ndn::Name(prefix),m_face,0);
  }
};
NS_OBJECT_ENSURE_REGISTERED(ControlApp);
} // namespace dtn
