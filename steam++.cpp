#include <cassert>
#include <cstdlib>
#include <cstring>

#include "cmclient.h"

using namespace Steam;

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
		auto output = new unsigned char[packetLength];
		
		auto success = EVP_CipherInit_ex(&cmClient->ctx, EVP_aes_256_ecb(), NULL, cmClient->sessionKey, NULL, 0);
		assert(success);
		
		unsigned char iv[16];
		
		EVP_CIPHER_CTX_set_padding(&cmClient->ctx, 0);
		int out_len;
		success = EVP_CipherUpdate(&cmClient->ctx, iv, &out_len, input, 16);
		assert(success);
		assert(out_len == 16);
		
		success = EVP_CipherInit_ex(&cmClient->ctx, EVP_aes_256_cbc(), NULL, cmClient->sessionKey, iv, 0);
		assert(success);
		
		auto crypted_data = input + 16;
		
		success = EVP_CipherUpdate(&cmClient->ctx, output, &out_len, crypted_data, packetLength - 16);
		assert(success);
		
		int out_len_final;
		success = EVP_CipherFinal_ex(&cmClient->ctx, output + out_len, &out_len_final);
		assert(success);
		
		ReadMessage(output, out_len + out_len_final);
		
		delete[] output;
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
