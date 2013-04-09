#include <cassert>
#include <cstdlib>
#include <iostream> // TEMPORARY
#include "steam++.h"
#include "steam_language/steam_language_internal.h"
#include "steammessages_clientserver.pb.h"

#include <openssl/rand.h>
#include <openssl/evp.h>

auto MAGIC = "VT01";
auto PROTO_MASK = 0x80000000;

struct {
	std::string host;
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

using namespace Steam;

SteamID::SteamID(std::uint64_t steamID64) :
	steamID64(steamID64) {}

SteamClient::SteamClient(
	std::function<void(const std::string& host, std::uint16_t port)> connect,
	std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write,
	std::function<void(std::function<void()> callback, int timeout)> set_interval
) :
	connect(std::move(connect)),
	write(std::move(write)),
	setInterval(std::move(set_interval)),
	packetLength(0),
	encrypted(false) { }

void SteamClient::LogOn(std::string username, std::string password, std::string code) {
	username = std::move(username);
	password = std::move(password);
	if (code.length()) {
		code = std::move(code);
	}
	
	steamID.instance = 1;
	steamID.universe = static_cast<unsigned>(EUniverse::Public);
	steamID.type = static_cast<unsigned>(EAccountType::Individual);
	
	auto &endpoint = servers[rand() % (sizeof(servers) / sizeof(servers[0]))];
	connect(endpoint.host, endpoint.port);
}

void SteamClient::SetPersonaState(EPersonaState state) {
	CMsgClientChangeStatus change_status;
	change_status.set_persona_state(static_cast<google::protobuf::uint32>(state));
	auto size = change_status.ByteSize();
	WriteMessage(EMsg::ClientChangeStatus, true, change_status.ByteSize(), [&change_status, size](unsigned char* buffer) {
		change_status.SerializeToArray(buffer, size);
	});
}

void SteamClient::JoinChat(SteamID chat) {
	if (chat.type == static_cast<unsigned>(EAccountType::Clan)) {
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	WriteMessage(EMsg::ClientJoinChat, false, sizeof(MsgClientJoinChat), [&chat](unsigned char* buffer) {
		auto join_chat = new (buffer) MsgClientJoinChat;
		join_chat->steamIdChat = chat.steamID64;
	});
}

void SteamClient::LeaveChat(SteamID chat) {
	// TODO: move this somwehre else
	if (chat.type == static_cast<unsigned>(EAccountType::Clan)) {
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	WriteMessage(EMsg::ClientChatMemberInfo, false, sizeof(MsgClientChatMemberInfo) + 20, [&](unsigned char* buffer) {
		auto leave_chat = new (buffer) MsgClientChatMemberInfo;
		leave_chat->steamIdChat = chat.steamID64;
		leave_chat->type = static_cast<unsigned>(EChatInfoType::StateChange);
		
		auto payload = buffer + sizeof(MsgClientChatMemberInfo);
		*reinterpret_cast<std::uint64_t*>(payload) = steamID.steamID64; // chatter_acted_on
		*reinterpret_cast<EChatMemberStateChange*>(payload + 8) = EChatMemberStateChange::Left; // state_change
		*reinterpret_cast<std::uint64_t*>(payload + 8 + 4) = steamID.steamID64; // chatter_acted_by
	});
}

void SteamClient::SendChatMessage(SteamID chat, const std::string& message) {
	// TODO: move this somwehre else
	if (chat.type == static_cast<unsigned>(EAccountType::Clan))	{
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	WriteMessage(EMsg::ClientChatMsg, false, sizeof(MsgClientChatMsg) + message.length() + 1, [&](unsigned char* buffer) {
		auto send_msg = new (buffer) MsgClientChatMsg;
		send_msg->chatMsgType = static_cast<std::uint32_t>(EChatEntryType::ChatMsg);
		send_msg->steamIdChatRoom = chat.steamID64;
		send_msg->steamIdChatter = steamID.steamID64;
		
		std::copy(message.cbegin(), message.cend(), buffer + sizeof(MsgClientChatMsg));
		buffer[sizeof(MsgClientChatMsg) + message.length()] = '\0';
	});
}

std::size_t SteamClient::connected() {
	std::cout << "Connected!" << std::endl; // TEMPORARY
	return 8;
}

std::size_t SteamClient::readable(const unsigned char* input) {
	if (!packetLength) {
		packetLength = *reinterpret_cast<const std::uint32_t*>(input);
		assert(std::equal(MAGIC, MAGIC + 4, input + 4));
		return packetLength;
	}
	
	if (encrypted) {
		EVP_CIPHER_CTX ctx;
		EVP_CIPHER_CTX_init(&ctx);
		auto output = new unsigned char[packetLength];
		
		auto success = EVP_CipherInit_ex(&ctx, EVP_aes_256_ecb(), NULL, sessionKey, NULL, 0);
		assert(success);
		
		unsigned char iv[16];
		
		EVP_CIPHER_CTX_set_padding(&ctx, 0);
		int out_len;
		success = EVP_CipherUpdate(&ctx, iv, &out_len, input, 16);
		assert(success);
		assert(out_len == 16);
		
		success = EVP_CipherInit_ex(&ctx, EVP_aes_256_cbc(), NULL, sessionKey, iv, 0);
		assert(success);
		
		auto crypted_data = input + 16;
		
		success = EVP_CipherUpdate(&ctx, output, &out_len, crypted_data, packetLength - 16);
		assert(success);
		
		int out_len_final;
		success = EVP_CipherFinal_ex(&ctx, output + out_len, &out_len_final);
		assert(success);
		
		ReadMessage(output, out_len + out_len_final);
		
		delete[] output;
		EVP_CIPHER_CTX_cleanup(&ctx);
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
		// TODO: do something with header
		HandleMessage(emsg, data + sizeof(MsgHdr), length - sizeof(MsgHdr));
	} else if (raw_emsg & PROTO_MASK) {
		auto header = reinterpret_cast<const MsgHdrProtoBuf*>(data);
		CMsgProtoBufHeader proto;
		proto.ParseFromArray(header->proto, header->headerLength);
		if (!sessionID && header->headerLength > 0) {
			sessionID = proto.client_sessionid();
			steamID = proto.steamid();
		}
		HandleMessage(emsg, data + sizeof(MsgHdrProtoBuf) + header->headerLength, length - sizeof(MsgHdrProtoBuf) - header->headerLength);
	} else {
		auto header = reinterpret_cast<const ExtendedClientMsgHdr*>(data);
		// TODO: you know
		HandleMessage(emsg, data + sizeof(ExtendedClientMsgHdr), length - sizeof(ExtendedClientMsgHdr));
	}
}

void SteamClient::WriteMessage(EMsg emsg, bool is_proto, std::size_t length, const std::function<void(unsigned char* buffer)> &fill) {
	if (emsg == EMsg::ChannelEncryptResponse) {
		WritePacket(sizeof(MsgHdr) + length, [emsg, &fill](unsigned char* buffer) {
			auto header = new (buffer) MsgHdr;
			header->msg = static_cast<std::uint32_t>(emsg);
			fill(buffer + sizeof(MsgHdr));
		});
	} else if (is_proto) {
		CMsgProtoBufHeader proto;
		proto.set_steamid(steamID.steamID64);
		proto.set_client_sessionid(sessionID);
		WritePacket(sizeof(MsgHdrProtoBuf) + proto.ByteSize() + length, [&proto, emsg, &fill](unsigned char* buffer) {
			auto header = new (buffer) MsgHdrProtoBuf;
			header->headerLength = proto.ByteSize();
			header->msg = static_cast<std::uint32_t>(emsg) | PROTO_MASK;
			proto.SerializeToArray(header->proto, header->headerLength);
			fill(header->proto + header->headerLength);
		});
	} else {
		WritePacket(sizeof(ExtendedClientMsgHdr) + length, [this, emsg, &fill](unsigned char* buffer) {
			auto header = new (buffer) ExtendedClientMsgHdr;
			header->msg = static_cast<std::uint32_t>(emsg);
			header->sessionID = sessionID;
			header->steamID = steamID.steamID64;
			fill(buffer + sizeof(ExtendedClientMsgHdr));
		});
	}
}

void SteamClient::WritePacket(const std::size_t length, const std::function<void(unsigned char* buffer)> &fill) {
	if (encrypted) {
		auto crypted_size = 16 + (length / 16 + 1) * 16; // IV + crypted message padded to multiple of 16
		
		write(8 + crypted_size, [&](unsigned char* out_buffer) {
			EVP_CIPHER_CTX ctx;
			EVP_CIPHER_CTX_init(&ctx);
			
			auto in_buffer = new unsigned char[length];
			fill(in_buffer);
			
			auto success = EVP_CipherInit_ex(&ctx, EVP_aes_256_ecb(), NULL, sessionKey, NULL, 1);
			assert(success);
			
			unsigned char iv[16];
			RAND_bytes(iv, sizeof(iv));
			
			auto crypted_iv = out_buffer + 8;
			
			EVP_CIPHER_CTX_set_padding(&ctx, 0);
			int out_len;
			success = EVP_CipherUpdate(&ctx, crypted_iv, &out_len, iv, sizeof(iv));
			assert(success);
			assert(out_len == 16);
			
			success = EVP_CipherInit_ex(&ctx, EVP_aes_256_cbc(), NULL, sessionKey, iv, 1);
			assert(success);
			
			auto crypted_data = crypted_iv + 16;
			success = EVP_CipherUpdate(&ctx, crypted_data, &out_len, in_buffer, length);
			assert(success);
			
			int out_len_final;
			success = EVP_CipherFinal_ex(&ctx, crypted_data + out_len, &out_len_final);
			assert(success);
			assert(out_len_final == 16);
			
			*reinterpret_cast<std::uint32_t*>(out_buffer) = crypted_size;
			std::copy(MAGIC, MAGIC + 4, out_buffer + 4);
			
			delete[] in_buffer;
			EVP_CIPHER_CTX_cleanup(&ctx);
		});
	} else {
		write(8 + length, [&](unsigned char* buffer) {
			*reinterpret_cast<std::uint32_t*>(buffer) = length;
			std::copy(MAGIC, MAGIC + 4, buffer + 4);
			fill(buffer + 8);
		});
	}
}
