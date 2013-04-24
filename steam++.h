#include <functional>
#include "steam_language/steam_language.h"

namespace Steam {
	const struct {
		const char* host;
		std::uint16_t port;
	} servers[] = {
		{ "72.165.61.174", 27017 },
		{ "72.165.61.174", 27018 },
		{ "72.165.61.175", 27017 },
		{ "72.165.61.175", 27018 },
		{ "72.165.61.176", 27017 },
		{ "72.165.61.176", 27018 },
		{ "72.165.61.185", 27017 },
		{ "72.165.61.185", 27018 },
		{ "72.165.61.187", 27017 },
		{ "72.165.61.187", 27018 },
		{ "72.165.61.188", 27017 },
		{ "72.165.61.188", 27018 },
		// Inteliquent, Luxembourg, cm-[01-04].lux.valve.net
		{ "146.66.152.12", 27017 },
		{ "146.66.152.12", 27018 },
		{ "146.66.152.12", 27019 },
		{ "146.66.152.13", 27017 },
		{ "146.66.152.13", 27018 },
		{ "146.66.152.13", 27019 },
		{ "146.66.152.14", 27017 },
		{ "146.66.152.14", 27018 },
		{ "146.66.152.14", 27019 },
		{ "146.66.152.15", 27017 },
		{ "146.66.152.15", 27018 },
		{ "146.66.152.15", 27019 },
		/* Highwinds, Netherlands (not live)
		{ "81.171.115.5", 27017 },
		{ "81.171.115.5", 27018 },
		{ "81.171.115.5", 27019 },
		{ "81.171.115.6", 27017 },
		{ "81.171.115.6", 27018 },
		{ "81.171.115.6", 27019 },
		{ "81.171.115.7", 27017 },
		{ "81.171.115.7", 27018 },
		{ "81.171.115.7", 27019 },
		{ "81.171.115.8", 27017 },
		{ "81.171.115.8", 27018 },
		{ "81.171.115.8", 27019 },*/
		// Highwinds, Kaysville
		{ "209.197.29.196", 27017 },
		{ "209.197.29.197", 27017 },
		/* Starhub, Singapore (non-optimal route)
		{ "103.28.54.10", 27017 },
		{ "103.28.54.11", 27017 }*/
	};
	
	struct SteamID {
		SteamID(std::uint64_t = 0);
		
		union {
			struct {
				unsigned ID : 32;
				unsigned instance : 20;
				unsigned type : 4;
				unsigned universe : 8;
			};
			std::uint64_t steamID64;
		};
	};
	
	class SteamClient {
	public:
		SteamClient(
			// called when SteamClient wants to send some data over the socket
			// allocate a buffer of `length` bytes, then call `fill` with it, then send it
			std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write,
			
			// `callback` must be called every `timeout` seconds as long as the connection is alive
			std::function<void(std::function<void()> callback, int timeout)> set_interval
		);
		
		
		/* slots */
		
		// connection has been established
		// returns the number of bytes SteamClient expects next
		std::size_t connected();
		
		// data has been received
		// `buffer` must be of the length previously returned by `connected` or `readable`
		// returns the number of bytes SteamClient expects next
		std::size_t readable(const unsigned char* buffer);
		
		
		/* signals */
		
		// encryption handshake complete
		// it's now safe to log on
		std::function<void()> onHandshake;
		
		// logon response received
		// EResult::OK means the logon was successful
		// anything else is an error and Steam should close the socket imminently
		std::function<void(EResult result)> onLogOn;
		
		// if LogOn was called without a hash, this is your new hash
		// you should save it and use it for your further logons - it will not expire unlike the code
		std::function<void(const unsigned char hash[20])> onSentry;
		
		// should be called in response to `JoinChat`
		// anything other than `EChatRoomEnterResponse::Success` denotes an error
		std::function<void(SteamID room, EChatRoomEnterResponse response)> onChatEnter;
		
		// a message has been received in a chat
		std::function<void(SteamID room, SteamID chatter, const char* message)> onChatMsg;
		
		
		/* methods */
		
		// call this after the encryption handshake (see onHandshake)
		void LogOn(
			const char* username,
			const char* password,
			
			// if your account uses Steam Guard, you should provide at least one of the below:
			
			// your sentry file hash (see onSentry)
			// if you have previously logged into another account, you can reuse its hash
			// otherwise, pass nullptr
			const unsigned char hash[20] = nullptr,
			
			// required if you are logging into this account for the first time
			// if not provided, onLogOn will get EResult::AccountLogonDenied and you will receive an email with the code
			const char* code = nullptr
		);
		
		// you'll want to call this with EPersonaState::Online upon logon to become visible
		void SetPersonaState(EPersonaState state);
		
		// see `onChatEnter`
		void JoinChat(SteamID chat);
		
		void LeaveChat(SteamID chat);
		
		void SendChatMessage(SteamID chat, const char* message);
		
	private:
		std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write;
		std::function<void(std::function<void()> callback, int timeout)> setInterval;
		
		SteamID steamID;
		std::int32_t sessionID;
		
		std::size_t packetLength;
		
		unsigned char sessionKey[32];
		bool encrypted;
		
		void ReadMessage(const unsigned char* data, std::size_t length);
		void HandleMessage(EMsg eMsg, const unsigned char* data, std::size_t length, std::uint64_t job_id);
		
		void WriteMessage(
			EMsg eMsg,
			bool is_proto,
			std::size_t length,
			const std::function<void(unsigned char* buffer)> &fill,
			std::uint64_t job_id = 0
		);
		void WritePacket(std::size_t length, const std::function<void(unsigned char* buffer)> &fill);
	};
}
