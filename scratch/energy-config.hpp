// Load and validate energy coefficients for communication, caching, and controller activity.
#pragma once
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace dtn {
struct Energy {
  // Byte and operation coefficients are joules; base and controllerBase are watts.
  double tx=1e-7, rx=5e-8, lookup=1e-8, insert=1e-7, refresh=5e-8, evict=2e-8;
  double base=0.001, collect=1e-6, compute=1e-5, controllerBase=0.001;
  double modelOp=1e-9;
  void Load(const std::string& path) {
    std::map<std::string, double*> keys{
      {"tx_j_per_byte",&tx},{"rx_j_per_byte",&rx},{"lookup_j",&lookup},
      {"insert_j",&insert},{"refresh_j",&refresh},{"evict_j",&evict},
      {"base_w_per_node",&base},{"collect_j_per_report",&collect},
      {"compute_j_per_round",&compute},{"controller_base_w",&controllerBase},
      {"model_op_j",&modelOp}};
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open energy config: " + path);
    std::string line;
    while (std::getline(f,line)) {
      // Accept trailing comments and blank lines in the key=value configuration.
      line = line.substr(0,line.find('#'));
      if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
      auto pos=line.find('=');
      if (pos==std::string::npos) throw std::runtime_error("invalid config line");
      std::string key=line.substr(0,pos);
      key.erase(std::remove_if(key.begin(),key.end(),[](char c){return c==' '||c=='\t';}),key.end());
      if (!keys.count(key)) throw std::runtime_error("unknown energy key: "+key);
      std::string value=line.substr(pos+1);
       size_t consumed=0;
      double v=std::stod(value,&consumed);
      // Reject trailing non-whitespace text, non-finite values, and negative costs.
      if (value.substr(consumed).find_first_not_of(" \t\r")!=std::string::npos || !std::isfinite(v)||v<0)
        throw std::runtime_error("invalid energy value: "+key);
      *keys[key]=v;
    }
  }
};
} // namespace dtn
