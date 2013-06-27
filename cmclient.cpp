#include "cmclient.h"

#include <openssl/rand.h>

const char* MAGIC = "VT01";
std::uint32_t PROTO_MASK = 0x80000000;

SteamClient::CMClient::CMClient(std::function<void(std::size_t, std::function<void(unsigned char*)>)> write) : write(std::move(write)) {
	steamID.instance = 1;
	steamID.universe = static_cast<unsigned>(EUniverse::Public);
	steamID.type = static_cast<unsigned>(EAccountType::Individual);
	EVP_CIPHER_CTX_init(&ctx);
}

SteamClient::CMClient::~CMClient() {
	EVP_CIPHER_CTX_cleanup(&ctx);
}

void SteamClient::CMClient::WriteMessage(EMsg emsg, std::size_t length, const std::function<void(unsigned char*)> &fill) {
	if (emsg == EMsg::ChannelEncryptResponse) {
		WritePacket(sizeof(MsgHdr) + length, [emsg, &fill](unsigned char* buffer) {
			auto header = new (buffer) MsgHdr;
			header->msg = static_cast<std::uint32_t>(emsg);
			fill(buffer + sizeof(MsgHdr));
		});
	} else {
		WritePacket(sizeof(ExtendedClientMsgHdr) + length, [this, emsg, &fill](unsigned char* buffer) {
			auto header = new (buffer) ExtendedClientMsgHdr;
			header->msg = static_cast<std::uint32_t>(emsg);
			header->sessionID = sessionID;
			header->steamID = steamID;
			fill(buffer + sizeof(ExtendedClientMsgHdr));
		});
	}
}

void SteamClient::CMClient::WriteMessage(EMsg emsg, const google::protobuf::Message &message, std::uint64_t job_id) {
	CMsgProtoBufHeader proto;
	proto.set_steamid(steamID);
	proto.set_client_sessionid(sessionID);
	if (job_id) {
		proto.set_jobid_target(job_id);
	}
	auto proto_size = proto.ByteSize();
	auto message_size = message.ByteSize();
	WritePacket(sizeof(MsgHdrProtoBuf) + proto_size + message_size, [emsg, &proto, proto_size, &message, message_size](unsigned char* buffer) {
		auto header = new (buffer) MsgHdrProtoBuf;
		header->headerLength = proto_size;
		header->msg = static_cast<std::uint32_t>(emsg) | PROTO_MASK;
		proto.SerializeToArray(header->proto, proto_size);
		message.SerializeToArray(header->proto + proto_size, message_size);
	});
}


void SteamClient::CMClient::WritePacket(const std::size_t length, const std::function<void(unsigned char* buffer)> &fill) {
	if (encrypted) {
		auto crypted_size = 16 + (length / 16 + 1) * 16; // IV + crypted message padded to multiple of 16
		
		write(8 + crypted_size, [&](unsigned char* out_buffer) {
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
		});
	} else {
		write(8 + length, [&](unsigned char* buffer) {
			*reinterpret_cast<std::uint32_t*>(buffer) = length;
			std::copy(MAGIC, MAGIC + 4, buffer + 4);
			fill(buffer + 8);
		});
	}
}
