#include <cryptopp/osrng.h>

#include "steam++.h"
#include "steam_language/steam_language_internal.h"
#include "steammessages_clientserver.pb.h"

extern const char* MAGIC;
extern std::uint32_t PROTO_MASK;

using namespace CryptoPP;
using namespace Steam;

class SteamClient::CMClient {
public:
	CMClient(std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write);
	
	void WriteMessage(Steam::EMsg emsg, std::size_t length, const std::function<void(unsigned char* buffer)> &fill);
	void WriteMessage(Steam::EMsg emsg, const google::protobuf::Message& message, std::uint64_t job_id = 0);
	void WritePacket(std::size_t length, const std::function<void(unsigned char* buffer)> &fill);
	
	std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write;	
	
	SteamID steamID;
	std::int32_t sessionID;

	bool encrypted;
	byte sessionKey[32];
	AutoSeededRandomPool rnd;
};
