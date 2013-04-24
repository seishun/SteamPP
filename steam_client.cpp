#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream> // TEMPORARY
#include "steam++.h"
#include "steam_language/steam_language_internal.h"
#include "steammessages_clientserver.pb.h"

#include <openssl/rand.h>
#include <openssl/evp.h>

auto MAGIC = "VT01";
auto PROTO_MASK = 0x80000000;

using namespace Steam;

SteamID::SteamID(std::uint64_t steamID64) :
	steamID64(steamID64) {}

SteamClient::SteamClient(
	std::function<void(std::size_t length, std::function<void(unsigned char* buffer)> fill)> write,
	std::function<void(std::function<void()> callback, int timeout)> set_interval
) : write(std::move(write)), setInterval(std::move(set_interval)) {
	steamID.instance = 1;
	steamID.universe = static_cast<unsigned>(EUniverse::Public);
	steamID.type = static_cast<unsigned>(EAccountType::Individual);
}

void SteamClient::LogOn(const char* username, const char* password, const char* code) {
	CMsgClientLogon logon;
	logon.set_account_name(username);
	logon.set_password(password);
	logon.set_protocol_version(65575);
	if (code) {
		logon.set_auth_code(code);
	}
	
	auto size = logon.ByteSize();
	WriteMessage(EMsg::ClientLogon, true, size, [&logon, size](unsigned char* buffer) {
		logon.SerializeToArray(buffer, size);
	});
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

void SteamClient::SendChatMessage(SteamID chat, const char* message) {
	// TODO: move this somwehre else
	if (chat.type == static_cast<unsigned>(EAccountType::Clan))	{
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}
	
	WriteMessage(EMsg::ClientChatMsg, false, sizeof(MsgClientChatMsg) + std::strlen(message) + 1, [&](unsigned char* buffer) {
		auto send_msg = new (buffer) MsgClientChatMsg;
		send_msg->chatMsgType = static_cast<std::uint32_t>(EChatEntryType::ChatMsg);
		send_msg->steamIdChatRoom = chat.steamID64;
		send_msg->steamIdChatter = steamID.steamID64;
		
		std::strcpy(reinterpret_cast<char*>(buffer + sizeof(MsgClientChatMsg)), message);
	});
}

std::size_t SteamClient::connected() {
	std::cout << "Connected!" << std::endl; // TEMPORARY
	
	steamID.ID = 0;
	sessionID = 0;
	packetLength = 0;
	encrypted = false;
	
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
		HandleMessage(emsg, data + sizeof(MsgHdr), length - sizeof(MsgHdr), header->sourceJobID);
	} else if (raw_emsg & PROTO_MASK) {
		auto header = reinterpret_cast<const MsgHdrProtoBuf*>(data);
		CMsgProtoBufHeader proto;
		proto.ParseFromArray(header->proto, header->headerLength);
		if (!sessionID && header->headerLength > 0) {
			sessionID = proto.client_sessionid();
			steamID = proto.steamid();
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

void SteamClient::WriteMessage(
	EMsg emsg,
	bool is_proto,
	std::size_t length,
	const std::function<void(unsigned char* buffer)> &fill,
	std::uint64_t job_id
) {
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
		if (job_id) {
			proto.set_jobid_target(job_id);
		}
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
