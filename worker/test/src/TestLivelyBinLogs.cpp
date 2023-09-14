#include <catch2/catch.hpp>
#include "LivelyBinLogs.hpp"

using namespace Utils;

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

