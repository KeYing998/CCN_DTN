// Read ndnSIM traffic and application-delay traces into aggregate byte and delay measurements.
#pragma once
#include <cmath>
#include <cstdint>
#include <istream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dtn {
struct Traffic { double tx=0,rx=0; };
inline std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> values;std::istringstream stream(line);std::string value;
  while(std::getline(stream,value,'\t')) values.push_back(value);
  return values;
}
// KilobytesRaw uses bytes / 1024 in ndnSIM 2.9. Select only actual network
// faces and the six traffic directions, never EWMA or satisfied-interest rows.
inline std::map<std::string,Traffic> ReadOfficialTraffic(
    std::istream& stream,const std::set<std::pair<std::string,std::string>>& faces) {
  std::string line;
  if(!std::getline(stream,line)) throw std::runtime_error("empty official L3 trace");
  auto header=SplitTabs(line);std::map<std::string,size_t> columns;
  for(size_t i=0;i<header.size();++i) columns[header[i]]=i;
  for(auto key:{"Node","FaceId","Type","KilobytesRaw"})
    if(!columns.count(key)) throw std::runtime_error("missing L3 trace column: "+std::string(key));
  std::map<std::string,Traffic> result;
  for(const auto& f:faces) result[f.first];
  while(std::getline(stream,line)) {
    if(line.empty())continue;
    auto cells=SplitTabs(line);
    if(cells.size()!=header.size()) throw std::runtime_error("malformed official L3 trace row");
    auto node=cells[columns["Node"]];auto face=cells[columns["FaceId"]];
    if(!faces.count({node,face}))continue;
    auto type=cells[columns["Type"]];
    bool tx=type=="OutInterests"||type=="OutData"||type=="OutNacks";
    bool rx=type=="InInterests"||type=="InData"||type=="InNacks";
    if(!tx&&!rx)continue;
    // Convert raw trace units of 1024 bytes to byte totals.
    double bytes=std::stod(cells[columns["KilobytesRaw"]])*1024;
    if(!std::isfinite(bytes)||bytes<0)throw std::runtime_error("invalid official traffic value");
    if(tx)result[node].tx+=bytes;else result[node].rx+=bytes;
  }
  return result;
}
struct Delays {uint64_t count=0;double sum=0;};
inline Delays ReadOfficialDelays(std::istream& stream) {
  std::string line;
  if(!std::getline(stream,line))throw std::runtime_error("empty official app delay trace");
  auto header=SplitTabs(line);std::map<std::string,size_t> columns;
  for(size_t i=0;i<header.size();++i)columns[header[i]]=i;
  for(auto key:{"Type","DelayS"})if(!columns.count(key))
    throw std::runtime_error("missing official delay column");
  Delays result;
  while(std::getline(stream,line)) {
    if(line.empty())continue;
    auto row=SplitTabs(line);
    if(row.size()!=header.size())throw std::runtime_error("malformed official delay row");
    // Count each completion once through FullDelay and omit last-send delay rows.
    if(row[columns["Type"]]!="FullDelay")continue;
    double delay=std::stod(row[columns["DelayS"]]);
    if(!std::isfinite(delay)||delay<0)throw std::runtime_error("invalid official delay value");
    ++result.count;result.sum+=delay;
  }
  return result;
}
} // namespace dtn
