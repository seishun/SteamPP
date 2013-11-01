#include <cryptopp/osrng.h>

#include <archive.h>
#include <archive_entry.h>

#include "steam++.h"
#include "steam_language/steam_language_internal.h"
#include "steammessages_clientserver.pb.h"

extern const char* MAGIC;
extern std::uint32_t PROTO_MASK;

using namespace CryptoPP;
using namespace Steam;

void create_session_key(AutoSeededRandomPool &rnd, unsigned char session_key[32], unsigned char crypted_sess_key[128]);

std::size_t crypted_length(std::size_t input_length);

// output must have at least crypted_length(input_length) bytes
void symmetric_encrypt(AutoSeededRandomPool &rnd, const unsigned char session_key[32], const unsigned char* input, std::size_t input_length, unsigned char* output);

std::string symmetric_decrypt(const unsigned char session_key[32], const unsigned char* input, std::size_t input_length);

std::string unzip(std::string &data);

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
