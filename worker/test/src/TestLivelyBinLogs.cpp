#include <catch2/catch.hpp>
#include "LivelyBinLogs.hpp"

SCENARIO("Grab userId from appData with userId")
{
    std::string jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","displayName":"display6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true,"userId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad"})";
    auto appData = nlohmann::json::parse(jsonString);
    std::string userId = Lively::GetUserIdFromAppData(appData);

    REQUIRE(userId == "6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad");
}

SCENARIO("Grab userId from appData with no userId, displayName")
{
    std::string jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","displayName":"display6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true})";
    auto appData = nlohmann::json::parse(jsonString);
    std::string userId = Lively::GetUserIdFromAppData(appData);

    REQUIRE(userId == "display6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad");
}

SCENARIO("Grab userId from appData with no userId, no displayName")
{
std::string jsonString = R"({"callId":"6d7fb685-0215-4f8a-af3a-29d5d4c1b7ad","iceConnected":true,"orientation":"landscape-primary","peerId":"8A46F3E0541B11EEBC973176C12C8EFA","streamName":"demo","trackEnabled":true})";
auto appData = nlohmann::json::parse(jsonString);
std::string userId = Lively::GetUserIdFromAppData(appData);

REQUIRE(userId == "");
}

SCENARIO("Producer binlog file names are created correctly.")
{
	auto callId = "call-id";
	auto producerId = "producer-id";
	auto userId = "12837401294";
	auto now = Utils::Time::currentStdEpochMs();
	auto version = "312b3129";

	std::string filename = Lively::ProducerFileName(callId, producerId, userId, now, version);

	REQUIRE(filename == "ms_p_call-id_producer-id_12837401294_" + std::to_string(now) + ".312b3129.bin");
}

SCENARIO("Consumer binlog file names are created correctly.")
{
	auto callId = "call-id";
	auto now = Utils::Time::currentStdEpochMs();
	auto version = "312b3129";

	std::string filename = Lively::ConsumerFileName(callId, now, version);

	REQUIRE(filename == "ms_c_call-id_" + std::to_string(now) + ".312b3129.bin");
}

