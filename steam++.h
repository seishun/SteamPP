#include <functional>
#include <string>
#include "steam_language/steam_language.h"

namespace Steam {
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
			// called when SteamClient wants to make a connection
			// call `connected()` once the connection has been established
			std::function<void(const std::string& host, std::uint16_t port)> connect,
			
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
		
		// logon response received
		// EResult::OK means the logon was successful
		// anything else is an error and Steam should close the socket imminently
		std::function<void(EResult result)> onLogOn;
		
		// should be called in response to `JoinChat`
		// anything other than `EChatRoomEnterResponse::Success` denotes an error
		std::function<void(SteamID room, EChatRoomEnterResponse response)> onChatEnter;
		
		// a message has been received in a chat
		std::function<void(SteamID room, SteamID chatter, std::string message)> onChatMsg;
		
		/* methods */

		// optionally, `code` is your Steam Guard code
		void LogOn(std::string username, std::string password, std::string code = "");
		
		// you'll want to call this with EPersonaState::Online upon logon to become visible
		void SetPersonaState(EPersonaState state);
		
		// see `onChatEnter`
		void JoinChat(SteamID chat);
		
		void LeaveChat(SteamID chat);
		
		void SendChatMessage(SteamID chat, const std::string& message);
		
	private:
		std::function<void(const std::string& host, std::uint16_t port)> connect;
		std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write;
		std::function<void(std::function<void()> callback, int timeout)> setInterval;
		
		std::string username;
		std::string password;
		std::string code;
		
		SteamID steamID;
		std::int32_t sessionID;
		
		std::size_t packetLength;
		
		unsigned char sessionKey[32];
		bool encrypted;
		
		void ReadMessage(const unsigned char* data, std::size_t length);
		void HandleMessage(EMsg eMsg, const unsigned char* data, std::size_t length);
		
		void WriteMessage(EMsg eMsg, bool isProto, std::size_t length, const std::function<void(unsigned char* buffer)> &fill);
		void WritePacket(std::size_t length, const std::function<void(unsigned char* buffer)> &fill);
	};
}
