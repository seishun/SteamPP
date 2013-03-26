#include <cassert>
#include <cstdlib>
#include <iostream> // TEMPORARY
#include "steam++.h"
#include "steam_language/steam_language_internal.h"
#include "steammessages_clientserver.pb.h"

#include <openssl/rand.h>
#include <openssl/evp.h>

auto MAGIC = "VT01";
auto protoMask = 0x80000000;

struct
{
	std::string host;
	std::uint16_t port;
}
servers[] =
{
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
	std::function<void(std::function<void()> callback, int timeout)> set_interval) :

	connect_(std::move(connect)),
	write_(std::move(write)),
	set_interval_(std::move(set_interval)),
	packet_length(0),
	encrypted(false) {}

void SteamClient::LogOn(std::string username, std::string password, std::string code)
{
	username_ = std::move(username);
	password_ = std::move(password);
	if (code.length())
	{
		code_ = std::move(code);
	}

	steamID.instance = 1;
	steamID.universe = static_cast<unsigned>(EUniverse::Public);
	steamID.type = static_cast<unsigned>(EAccountType::Individual);

	auto &endpoint = servers[rand() % (sizeof(servers) / sizeof(servers[0]))];
	connect_(endpoint.host, endpoint.port);
}

void SteamClient::SetPersonaState(EPersonaState state)
{
	CMsgClientChangeStatus changeStatus;
	changeStatus.set_persona_state(static_cast<google::protobuf::uint32>(state));
	auto size = changeStatus.ByteSize();
	WriteMessage(EMsg::ClientChangeStatus, true, changeStatus.ByteSize(), [&changeStatus, size](unsigned char* buffer)
	{
		changeStatus.SerializeToArray(buffer, size);
	});
}

void SteamClient::JoinChat(SteamID chat)
{
	if (chat.type == static_cast<unsigned>(EAccountType::Clan))
	{
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}

	WriteMessage(EMsg::ClientJoinChat, false, sizeof(MsgClientJoinChat), [&chat](unsigned char* buffer)
	{
		auto joinChat = new (buffer) MsgClientJoinChat;
		joinChat->steamIdChat = chat.steamID64;
	});
}

void SteamClient::SendChatMessage(SteamID chat, const std::string& message)
{
	// TODO: move this somwehre else
	if (chat.type == static_cast<unsigned>(EAccountType::Clan))
	{
		// this is a ClanID - convert to its respective ChatID
		chat.instance = static_cast<unsigned>(0x100000 >> 1); // TODO: this should be defined somewhere else
		chat.type = static_cast<unsigned>(EAccountType::Chat);
	}

	WriteMessage(EMsg::ClientChatMsg, false, sizeof(MsgClientChatMsg) + message.length() + 1, [&](unsigned char* buffer)
	{
		auto sendMsg = new (buffer) MsgClientChatMsg;
		sendMsg->chatMsgType = static_cast<std::uint32_t>(EChatEntryType::ChatMsg);
		sendMsg->steamIdChatRoom = chat.steamID64;
		sendMsg->steamIdChatter = steamID.steamID64;

		std::copy(message.cbegin(), message.cend(), buffer + sizeof(MsgClientChatMsg));
		buffer[sizeof(MsgClientChatMsg) + message.length()] = '\0';
	});
}

std::size_t SteamClient::connected()
{
	std::cout << "Connected!" << std::endl; // TEMPORARY
	return 8;
}

std::size_t SteamClient::readable(const unsigned char* input)
{
	if (!packet_length)
	{
		packet_length = *reinterpret_cast<const std::uint32_t*>(input);
		assert(std::equal(MAGIC, MAGIC + 4, input + 4));
		return packet_length;
	}

	if (encrypted)
	{
		EVP_CIPHER_CTX ctx;
		EVP_CIPHER_CTX_init(&ctx);
		auto output = new unsigned char[packet_length];

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

		success = EVP_CipherUpdate(&ctx, output, &out_len, crypted_data, packet_length - 16);
		assert(success);

		int out_len_final;
		success = EVP_CipherFinal_ex(&ctx, output + out_len, &out_len_final);
		assert(success);

		ReadMessage(output, out_len + out_len_final);

		delete[] output;
		EVP_CIPHER_CTX_cleanup(&ctx);
	}
	else
	{
		ReadMessage(input, packet_length);
	}
	packet_length = 0;
	return 8;
}

void SteamClient::ReadMessage(const unsigned char* data, std::size_t length)
{
	auto rawEMsg = *reinterpret_cast<const std::uint32_t*>(data);
	auto eMsg = static_cast<EMsg>(rawEMsg & ~protoMask);

	// first figure out the header type
	if (eMsg == EMsg::ChannelEncryptRequest || eMsg == EMsg::ChannelEncryptResult)
	{
		auto header = reinterpret_cast<const MsgHdr*>(data);
		// TODO: do something with header
		HandleMessage(eMsg, data + sizeof(MsgHdr), length - sizeof(MsgHdr));
	}
	else if (rawEMsg & protoMask)
	{
		auto header = reinterpret_cast<const MsgHdrProtoBuf*>(data);
		CMsgProtoBufHeader proto;
		proto.ParseFromArray(header->proto, header->headerLength);
		if (!sessionID && header->headerLength > 0)
		{
			sessionID = proto.client_sessionid();
			steamID = proto.steamid();
		}

		HandleMessage(eMsg, data + sizeof(MsgHdrProtoBuf) + header->headerLength, length - sizeof(MsgHdrProtoBuf) - header->headerLength);
	}
	else
	{
		auto header = reinterpret_cast<const ExtendedClientMsgHdr*>(data);
		// TODO: you know
		HandleMessage(eMsg, data + sizeof(ExtendedClientMsgHdr), length - sizeof(ExtendedClientMsgHdr));
	}
}

void SteamClient::WriteMessage(EMsg eMsg, bool isProto, size_t length, const std::function<void(unsigned char* buffer)> &fill)
{
	if (eMsg == EMsg::ChannelEncryptResponse)
	{
		WritePacket(sizeof(MsgHdr) + length, [eMsg, &fill](unsigned char* buffer)
		{
			auto header = new (buffer) MsgHdr;
			header->msg = static_cast<std::uint32_t>(eMsg);
			fill(buffer + sizeof(MsgHdr));
		});
	}
	else if (isProto)
	{
		CMsgProtoBufHeader proto;
		proto.set_steamid(steamID.steamID64);
		proto.set_client_sessionid(sessionID);
		WritePacket(sizeof(MsgHdrProtoBuf) + proto.ByteSize() + length, [&proto, eMsg, &fill](unsigned char* buffer)
		{
			auto header = new (buffer) MsgHdrProtoBuf;
			header->headerLength = proto.ByteSize();
			header->msg = static_cast<std::uint32_t>(eMsg) | protoMask;
			proto.SerializeToArray(header->proto, header->headerLength);
			fill(header->proto + header->headerLength);
		});
	}
	else
	{
		WritePacket(sizeof(ExtendedClientMsgHdr) + length, [this, eMsg, &fill](unsigned char* buffer)
		{
			auto header = new (buffer) ExtendedClientMsgHdr;
			header->msg = static_cast<std::uint32_t>(eMsg);
			header->sessionID = sessionID;
			header->steamID = steamID.steamID64;
			fill(buffer + sizeof(ExtendedClientMsgHdr));
		});
	}
}

void SteamClient::WritePacket(const size_t length, const std::function<void(unsigned char* buffer)> &fill)
{
	if (encrypted)
	{
		auto crypted_size = 16 + (length / 16 + 1) * 16; // IV + crypted message padded to multiple of 16

		write_(8 + crypted_size, [&](unsigned char* out_buffer)
		{
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
	}
	else
	{
		write_(8 + length, [&](unsigned char* buffer)
		{
			*reinterpret_cast<std::uint32_t*>(buffer) = length;
			std::copy(MAGIC, MAGIC + 4, buffer + 4);
			fill(buffer + 8);
		});
	}
}
