#ifndef MS_LIVELY_HPP
#define MS_LIVELY_HPP

#include <string>

namespace Lively
{
  class AppData
  {
  public:
    std::string callId;
    std::string peerId;
    std::string mirrorId;
    std::string streamName;
//    std::string userId;
    std::string id; // passed separately

  public:
    AppData() = default;
//    AppData(const AppData& d) : callId(d.callId), peerId(d.peerId), mirrorId(d.mirrorId), streamName(d.streamName), id(d.id) {}
    
//    AppData& operator=(const AppData& d) { callId = d.callId; peerId = d.peerId; mirrorId = d.mirrorId; streamName = d.streamName; id = d.id; return *this; }

    AppData(const AppData& d) = default;
    AppData& operator=(const AppData& d) = default;

    std::string ToStr() const;
  };
}; // namespace Lively
#endif