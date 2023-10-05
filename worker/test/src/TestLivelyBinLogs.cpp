#include <catch2/catch.hpp>
#include "LivelyBinLogs.hpp"

SCENARIO("Grab userId from appData with userId")
{
    std::string const jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","displayName":"display6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true,"userId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad"})";
    auto appData = nlohmann::json::parse(jsonString);
    std::string const userId = Lively::GetUserIdFromAppData(appData);

    REQUIRE(userId == "6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad");
}

SCENARIO("Grab userId from appData with no userId, displayName")
{
    std::string const jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","displayName":"display6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true})";
    auto appData = nlohmann::json::parse(jsonString);
    std::string const userId = Lively::GetUserIdFromAppData(appData);

    REQUIRE(userId.empty());
}

SCENARIO("Grab userId from appData with no userId, no displayName")
{
    std::string const jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true})";
    auto appData = nlohmann::json::parse(jsonString);
    std::string const userId = Lively::GetUserIdFromAppData(appData);

    REQUIRE(userId.empty());
}

SCENARIO("Grab userId from appData with userId invalid characters")
{
    std::string const jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","displayName":"display6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true,"userId":"6:7fb685-0215-4f8a-af3a-29d5d&c1b7a."})";
    auto appData = nlohmann::json::parse(jsonString);
    std::string const userId = Lively::GetUserIdFromAppData(appData);

    REQUIRE(userId == "6-7fb685-0215-4f8a-af3a-29d5d-c1b7a-");
}

SCENARIO("Producer binlog file names are created correctly.")
{
    const auto *callId = "call-id";
    const auto *producerId = "producer-id";
    const auto *userId = "12837401294";
    auto now = Utils::Time::currentStdEpochMs();
    const auto *version = "312b3129";

    std::string const filename = Lively::ProducerFileName(callId, producerId, userId, now, version);

    REQUIRE(filename == "ms_p_12837401294_call-id_producer-id_" + std::to_string(now) + ".312b3129.bin");
}

SCENARIO("Consumer binlog file names are created correctly.")
{
    const auto *callId = "call-id";
    auto now = Utils::Time::currentStdEpochMs();
    const auto *version = "312b3129";

    std::string const filename = Lively::ConsumerFileName(callId, now, version);

    REQUIRE(filename == "ms_c_call-id_" + std::to_string(now) + ".312b3129.bin");
}

