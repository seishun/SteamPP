#include <cassert>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "cmclient.h"

std::map<std::string, std::string> Steam::parse_VDF(const std::string& text) {
	std::istringstream text_stream(text);
	std::map<std::string, std::string> kv;

	// we want to know the indentation of nested VDFs, so read line by line to avoid eating an opening brace
	std::string line;
	while (std::getline(text_stream, line)) {
		std::istringstream line_stream(line);
		std::string key, value;
		line_stream >> key >> value;

		// if the value is a nested VDF, store it as a string
		if (value.empty()) {
			// the { should be on the next line
			std::string brace;
			std::getline(text_stream, brace);
			assert(brace[brace.length() - 1] == '{');
			brace[brace.length() - 1] = '}';

			std::ostringstream nested_vdf;
			std::ostream_iterator<std::string> it(nested_vdf, "\n");

			// append everything until we see the matching closing brace
			std::string line;
			while (std::getline(text_stream, line) && line != brace)
				*it++ = line;

			value = nested_vdf.str();
		} else {
			value = value.substr(1, value.length() - 2);
		}

		kv[key.substr(1, key.length() - 2)] = std::move(value);
	}

	return kv;
}

std::map<std::string, std::string> Steam::operator*(const std::string& text) {
	return parse_VDF(text);
}

SteamID::SteamID(std::uint64_t steamID64) :
	steamID64(steamID64) {}

SteamID::operator std::uint64_t() const {
	return steamID64;
}

SteamClient::SteamClient(
	std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write,
	std::function<void(std::function<void()> callback, int timeout)> set_interval
) : cmClient(new CMClient(std::move(write))), setInterval(std::move(set_interval)) {}

SteamClient::~SteamClient() {
	delete cmClient;
}

void SteamClient::LogOn(const char* username, const char* password, const unsigned char hash[20], const char* code, SteamID steamID) {
	if (steamID)
		cmClient->steamID = steamID;
	
	CMsgClientLogon logon;
	logon.set_account_name(username);
	logon.set_password(password);
	logon.set_protocol_version(65575);
	if (hash) {
		logon.set_sha_sentryfile(hash, 20);
	}
	if (code) {
		logon.set_auth_code(code);
	}
	cmClient->WriteMessage(EMsg::ClientLogon, logon);
}

void SteamClient::SetPersonaState(EPersonaState state) {
	CMsgClientChangeStatus change_status;
	change_status.set_persona_state(static_cast<google::protobuf::uint32>(state));
	cmClient->WriteMessage(EMsg::ClientChangeStatus, change_status);
}

void SteamClient::JoinChat(SteamID chat) {
	if (chat.type == static_cast<unsigned>(EAccountType::Clan)) {
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	cmClient->WriteMessage(EMsg::ClientJoinChat, sizeof(MsgClientJoinChat), [&chat](unsigned char* buffer) {
		auto join_chat = new (buffer) MsgClientJoinChat;
		join_chat->steamIdChat = chat;
	});
}

void SteamClient::LeaveChat(SteamID chat) {
	// TODO: move this somwehre else
	if (chat.type == static_cast<unsigned>(EAccountType::Clan)) {
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	cmClient->WriteMessage(EMsg::ClientChatMemberInfo, sizeof(MsgClientChatMemberInfo) + 20, [&](unsigned char* buffer) {
		auto leave_chat = new (buffer) MsgClientChatMemberInfo;
		leave_chat->steamIdChat = chat;
		leave_chat->type = static_cast<unsigned>(EChatInfoType::StateChange);
		
		auto payload = buffer + sizeof(MsgClientChatMemberInfo);
		*reinterpret_cast<std::uint64_t*>(payload) = cmClient->steamID; // chatter_acted_on
		*reinterpret_cast<EChatMemberStateChange*>(payload + 8) = EChatMemberStateChange::Left; // state_change
		*reinterpret_cast<std::uint64_t*>(payload + 8 + 4) = cmClient->steamID; // chatter_acted_by
	});
}

void SteamClient::SendChatMessage(SteamID chat, const char* message) {
	// TODO: move this somwehre else
	if (chat.type == static_cast<unsigned>(EAccountType::Clan))	{
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	cmClient->WriteMessage(EMsg::ClientChatMsg, sizeof(MsgClientChatMsg) + std::strlen(message) + 1, [&](unsigned char* buffer) {
		auto send_msg = new (buffer) MsgClientChatMsg;
		send_msg->chatMsgType = static_cast<std::uint32_t>(EChatEntryType::ChatMsg);
		send_msg->steamIdChatRoom = chat;
		send_msg->steamIdChatter = cmClient->steamID;
		
		std::strcpy(reinterpret_cast<char*>(buffer + sizeof(MsgClientChatMsg)), message);
	});
}

void SteamClient::SendPrivateMessage(SteamID user, const char* message) {
	CMsgClientFriendMsg msg;
	
	msg.set_steamid(user);
	msg.set_message(message);
	msg.set_chat_entry_type(static_cast<google::protobuf::uint32>(EChatEntryType::ChatMsg));
	
	cmClient->WriteMessage(EMsg::ClientFriendMsg, msg);
}

void SteamClient::SendTyping(SteamID user) {
	CMsgClientFriendMsg msg;
	
	msg.set_steamid(user);
	msg.set_chat_entry_type(static_cast<google::protobuf::uint32>(EChatEntryType::Typing));
	
	cmClient->WriteMessage(EMsg::ClientFriendMsg, msg);
}

void SteamClient::RequestUserInfo(std::size_t count, SteamID users[]) {
	CMsgClientRequestFriendData request;
	
	while (count--)
		request.add_friends(users[count]);
	
	// TODO: allow custom flags
	request.set_persona_state_requested(282);
	
	cmClient->WriteMessage(EMsg::ClientRequestFriendData, request);
}

void SteamClient::GetAppOwnershipTicket(std::uint32_t appid) {
	CMsgClientGetAppOwnershipTicket request;

	request.set_app_id(appid);

	cmClient->WriteMessage(EMsg::ClientGetAppOwnershipTicket, request);
}

void SteamClient::PICSGetProductInfo(std::uint32_t app) {
	CMsgPICSProductInfoRequest request;

	request.add_apps()->set_appid(app);

	cmClient->WriteMessage(EMsg::PICSProductInfoRequest, request);
}

void SteamClient::GetDepotDecryptionKey(std::uint32_t depotid) {
	CMsgClientGetDepotDecryptionKey request;

	request.set_depot_id(depotid);

	cmClient->WriteMessage(EMsg::ClientGetDepotDecryptionKey, request);
}


std::size_t SteamClient::connected() {
	packetLength = 0;	
	cmClient->steamID.ID = 0;
	cmClient->sessionID = 0;
	cmClient->encrypted = false;
	
	return 8;
}

std::size_t SteamClient::readable(const unsigned char* input) {
	if (!packetLength) {
		packetLength = *reinterpret_cast<const std::uint32_t*>(input);
		assert(std::equal(MAGIC, MAGIC + 4, input + 4));
		return packetLength;
	}
	
	if (cmClient->encrypted) {
		auto output = symmetric_decrypt(cmClient->sessionKey, input, packetLength);
		ReadMessage(reinterpret_cast<const unsigned char*>(output.data()), output.length());
	} else {
		ReadMessage(input, packetLength);
	}
	
	packetLength = 0;
	return 8;
}

void SteamClient::ReadMessage(const unsigned char* data, std::size_t length) {
	auto raw_emsg = *reinterpret_cast<const std::uint32_t*>(data);
	auto emsg = static_cast<EMsg>(raw_emsg & ~PROTO_MASK);
	
	// first figure out the header type
	if (emsg == EMsg::ChannelEncryptRequest || emsg == EMsg::ChannelEncryptResult) {
		auto header = reinterpret_cast<const MsgHdr*>(data);
		HandleMessage(emsg, data + sizeof(MsgHdr), length - sizeof(MsgHdr), header->sourceJobID);
	} else if (raw_emsg & PROTO_MASK) {
		auto header = reinterpret_cast<const MsgHdrProtoBuf*>(data);
		CMsgProtoBufHeader proto;
		proto.ParseFromArray(header->proto, header->headerLength);
		if (!cmClient->sessionID && header->headerLength > 0) {
			cmClient->sessionID = proto.client_sessionid();
			cmClient->steamID = proto.steamid();
		}
		HandleMessage(
			emsg,
			data + sizeof(MsgHdrProtoBuf) + header->headerLength,
			length - sizeof(MsgHdrProtoBuf) - header->headerLength,
			proto.jobid_source()
		);
	} else {
		auto header = reinterpret_cast<const ExtendedClientMsgHdr*>(data);
		HandleMessage(emsg, data + sizeof(ExtendedClientMsgHdr), length - sizeof(ExtendedClientMsgHdr), header->sourceJobID);
	}
}
