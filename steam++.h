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
			std::function<void(const std::string& host, std::uint16_t port)> connect,
			std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write,
			std::function<void(std::function<void()> callback, int timeout)> setInterval
		);
		
		// slots
		std::size_t connected();
		std::size_t readable(const unsigned char* buffer);
		
		// signals
		std::function<void()> onLogOn;
		std::function<void(SteamID source, std::string message)> onChatMsg;
		
		// methods
		void LogOn(std::string username, std::string password, std::string code = "");
		void SetPersonaState(EPersonaState state);
		void JoinChat(SteamID chat);
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
