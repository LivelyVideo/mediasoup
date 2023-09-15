#include "Lively.hpp"
#include <nlohmann/json.hpp>
#include "LivelyAppDataToJson.hpp"

using json = nlohmann::json;

namespace Lively {
  std::string AppData::ToStr() const 
  {
      return "callId=\"" + callId + "\""
             + " peerId=\"" + peerId + "\""
             + " mirrorId=\"" + mirrorId + "\""
             + " streamName=\"" + streamName + "\""
             + " objectId=\"" + id + "\""
             + " userId=\"" + userId + "\"";
  }
}