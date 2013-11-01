#include <functional>
#include <map>
#include <queue>
#include <vector>
#include "steam_language/steam_language.h"

namespace Steam {
	// a very simple, non-recursive parser that doesn't support comments or unquoted strings and assumes consistent indentation
	std::map<std::string, std::string> parse_VDF(const std::string& text);

	// shortcut for parse_VDF
	std::map<std::string, std::string> operator*(const std::string& text);

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
		operator std::uint64_t() const;
		
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
	
#pragma pack(push, 1)
	struct ChatMember {
	private:
		enum Type : char {
			None = 0,
		    String = 1,
		    Int32 = 2,
		    Float32 = 3,
		    Pointer = 4,
		    WideString = 5,
		    Color = 6,
		    UInt64 = 7,
		    End = 8
		};
		
		Type None_;
		char MessageObject_[sizeof("MessageObject")];
		
		Type UInt64_;
		char steamid_[sizeof("steamid")];
		
	public:
		SteamID steamID;
		
	private:
		Type Int32_;
		char Permissions_[sizeof("Permissions")];
		
	public:
		EChatPermission permissions;
		
	private:
		Type Int32__;
		char Details_[sizeof("Details")];
		
	public:
		EClanPermission rank;
		
	private:
		Type End_;
		Type End__;
	};
#pragma pack(pop)
	
	class SteamClient {
	public:
		/**
		 * @param write         Called when SteamClient wants to send some data over the socket.
		 *                      Allocate a buffer of @a length bytes, then call @a fill with it, then send it.
		 * @param set_interval  @a callback must be called every @a timeout seconds as long as the connection is alive.
		 */
		SteamClient(
			std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write,
			std::function<void(std::function<void()> callback, int timeout)> set_interval
		);
		
		~SteamClient();
		
		
		/**
		 * Call when a connection has been established.
		 * 
		 * @return The number of bytes SteamClient expects next.
		 */
		std::size_t connected();
		
		/**
		 * Call when data has been received.
		 * 
		 * @param buffer    Must be of the length previously returned by #connected or #readable.
		 * @return The number of bytes SteamClient expects next.
		 */
		std::size_t readable(const unsigned char* buffer);
		
		
		/**
		 * Encryption handshake complete – it's now safe to log on.
		 */
		std::function<void()> onHandshake;
		
		/**
		 * @a steamID is your SteamID.
		 */
		std::function<void(EResult result, SteamID steamID)> onLogOn;
		
		std::function<void(EResult result)> onLogOff;
		
		std::function<void(const unsigned char hash[20])> onSentry;
		
		/**
		 * Each parameter except @a user is optional and will equal @c nullptr if unset.
		 */
		std::function<void(
			SteamID user,
			SteamID* source,
			const char* name,
			EPersonaState* state,
			const unsigned char avatar_hash[20],
			const char* game_name
		)> onUserInfo;
		
		/**
		 * Should be called in response to #JoinChat.
		 */
		std::function<void(
			SteamID room,
			EChatRoomEnterResponse response,
			const char* name,
			std::size_t member_count,
			const ChatMember members[]
		)> onChatEnter;
		
		/**
		 * @a member is invalid unless @a state_change == @c EChatMemberStateChange::Entered.
		 */
		std::function<void(
			SteamID room,
			SteamID acted_by,
			SteamID acted_on,
			EChatMemberStateChange state_change,
			const ChatMember* member
		)> onChatStateChange;
		
		std::function<void(SteamID room, SteamID chatter, const char* message)> onChatMsg;
		
		std::function<void(SteamID user, const char* message)> onPrivateMsg;
		
		std::function<void(SteamID user)> onTyping;
		
		std::function<void(
			bool incremental,
			std::map<SteamID, EFriendRelationship> &users,
			std::map<SteamID, EClanRelationship> &groups
		)> onRelationships;
		
		std::function<void(EResult result, std::uint32_t appID, std::string &ticket)> onAppOwnershipTicket;

		/**
		 * Us jkdsljf;asdk;lf a
		 */
		std::function<void(std::uint32_t appID, std::string &key_values)> onPICSProductInfo;

		std::function<void(EResult result, std::uint32_t depotID, std::string &depot_key)> onDepotKey;
		
		
		/**
		 * Call this after the encryption handshake. @a steamID is only needed if you are logging into a non-default instance.
		 * 
		 * @see onHandshake
		 */
		void LogOn(
			const char* username,
			const char* password,
			const unsigned char sentry_hash[20] = nullptr,
			const char* code = nullptr,
			SteamID steamID = 0
		);
		
		void SetPersonaState(EPersonaState state);
		
		/**
		 * @see onChatEnter
		 */
		void JoinChat(SteamID chat);
		
		void LeaveChat(SteamID chat);
		
		void SendChatMessage(SteamID chat, const char* message);
		
		void SendPrivateMessage(SteamID user, const char* message);
		
		void SendTyping(SteamID user);
		
		/**
		 * @see onUserInfo
		 */
		void RequestUserInfo(std::size_t count, SteamID users[]);

		void GetAppOwnershipTicket(std::uint32_t appid);

		void PICSGetProductInfo(std::uint32_t app);

		void GetDepotDecryptionKey(std::uint32_t depotid);
		
	private:
		class CMClient;
		CMClient* cmClient;
		
		// some members remain here to avoid a back pointer
		std::function<void(std::function<void()> callback, int timeout)> setInterval;
		std::size_t packetLength;
		void ReadMessage(const unsigned char* data, std::size_t length);
		void HandleMessage(EMsg eMsg, const unsigned char* data, std::size_t length, std::uint64_t job_id);
	};

	class CSClientPool {
		typedef std::function<void(
			const char* uri,
			const char* method,
			std::vector<std::pair<const char*, const char*>> headers,
			std::vector<std::tuple<const char*, const unsigned char*, std::size_t>> query,
			std::function<void(int status_code, std::string &body)> callback
		)> http_request_t;

	public:
		struct ChunkData {
			std::string chunk_ID;
			std::uint64_t offset;
			std::uint32_t size;
		};

		struct FileData {
			std::string filename;
			std::vector<ChunkData> chunks;
			EDepotFileFlag flags;
			std::uint64_t size;
		};

		CSClientPool(std::vector<std::string> servers, const unsigned char* app_ticket, std::size_t app_ticket_length, http_request_t http_request);
		// TODO: add destructor

		/*
		The callback receives a list of files.
		A file consists of filename, size, flags and a list of chunks.
		*/
		void DownloadDepotManifest(
			std::uint32_t depotid,
			std::uint64_t manifestid,
			std::string app_ticket,
			std::string depot_key,
			std::function<void(std::vector<FileData> &files)> callback
		);

		void DownloadDepotChunk(std::uint32_t depotid, std::string chunkid, std::string app_ticket, std::string depot_key, std::function<void(std::string chunk)>);

		static void FetchServerList(
			const char* host,
			std::uint16_t port,
			int cell_id,
			const http_request_t &http_request,
			std::function<void(std::vector<std::string> &server_list)> callback
		);

	private:
		http_request_t http_request;

		struct CSClient;

		std::vector<CSClient*> clients;

		std::queue<std::function<void(CSClient &client)>> queue;

		void DoOrQueue(std::uint32_t depotid, std::string app_ticket, std::string uri, std::function<void(std::string body)> callback);

		static std::string make_header(CSClient &client, const std::string &uri);
	};
}
