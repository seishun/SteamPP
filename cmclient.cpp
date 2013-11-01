#include <cryptopp/modes.h>
#include <cryptopp/rsa.h>

#include "cmclient.h"

const char* MAGIC = "VT01";
std::uint32_t PROTO_MASK = 0x80000000;

byte public_key[] = {
	0x30, 0x81, 0x9D, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01,
	0x05, 0x00, 0x03, 0x81, 0x8B, 0x00, 0x30, 0x81, 0x87, 0x02, 0x81, 0x81, 0x00, 0xDF, 0xEC, 0x1A,
	0xD6, 0x2C, 0x10, 0x66, 0x2C, 0x17, 0x35, 0x3A, 0x14, 0xB0, 0x7C, 0x59, 0x11, 0x7F, 0x9D, 0xD3,
	0xD8, 0x2B, 0x7A, 0xE3, 0xE0, 0x15, 0xCD, 0x19, 0x1E, 0x46, 0xE8, 0x7B, 0x87, 0x74, 0xA2, 0x18,
	0x46, 0x31, 0xA9, 0x03, 0x14, 0x79, 0x82, 0x8E, 0xE9, 0x45, 0xA2, 0x49, 0x12, 0xA9, 0x23, 0x68,
	0x73, 0x89, 0xCF, 0x69, 0xA1, 0xB1, 0x61, 0x46, 0xBD, 0xC1, 0xBE, 0xBF, 0xD6, 0x01, 0x1B, 0xD8,
	0x81, 0xD4, 0xDC, 0x90, 0xFB, 0xFE, 0x4F, 0x52, 0x73, 0x66, 0xCB, 0x95, 0x70, 0xD7, 0xC5, 0x8E,
	0xBA, 0x1C, 0x7A, 0x33, 0x75, 0xA1, 0x62, 0x34, 0x46, 0xBB, 0x60, 0xB7, 0x80, 0x68, 0xFA, 0x13,
	0xA7, 0x7A, 0x8A, 0x37, 0x4B, 0x9E, 0xC6, 0xF4, 0x5D, 0x5F, 0x3A, 0x99, 0xF9, 0x9E, 0xC4, 0x3A,
	0xE9, 0x63, 0xA2, 0xBB, 0x88, 0x19, 0x28, 0xE0, 0xE7, 0x14, 0xC0, 0x42, 0x89, 0x02, 0x01, 0x11,
};

void create_session_key(AutoSeededRandomPool &rnd, unsigned char session_key[32], unsigned char crypted_sess_key[128]) {
	RSA::PublicKey key;
	ArraySource source(public_key, sizeof(public_key), true /* pumpAll */);
	key.Load(source);
	RSAES_OAEP_SHA_Encryptor rsa(key);

	rnd.GenerateBlock(session_key, 32);
	rsa.Encrypt(rnd, session_key, 32, crypted_sess_key);
}

std::size_t crypted_length(std::size_t input_length) {
	return 16 + (input_length / 16 + 1) * 16; // IV + crypted message padded to multiple of 16
}

void symmetric_encrypt(AutoSeededRandomPool &rnd, const unsigned char session_key[32], const unsigned char* input, std::size_t input_length, unsigned char* output) {
	byte iv[16];
	rnd.GenerateBlock(iv, 16);
	
	ECB_Mode<AES>::Encryption(session_key, 32).ProcessData(output, iv, sizeof(iv));
	
	auto crypted_data = output + 16;
	CBC_Mode<AES>::Encryption e(session_key, 32, iv);
	ArraySource(
		input,
		input_length,
		true,
		new StreamTransformationFilter(e, new ArraySink(crypted_data, (input_length / 16 + 1) * 16))
	);
}

std::string symmetric_decrypt(const unsigned char session_key[32], const unsigned char* input, std::size_t input_length) {
	byte iv[16];
	ECB_Mode<AES>::Decryption(session_key, 32).ProcessData(iv, input, 16);
		
	auto crypted_data = input + 16;
	CBC_Mode<AES>::Decryption d(session_key, 32, iv);
	// I don't see any way to get the decrypted size other than to use a string
	std::string output;
	ArraySource(
		crypted_data,
		input_length - 16,
		true,
		new StreamTransformationFilter(d, new StringSink(output))
	);

	return output;
}

std::string unzip(std::string &input) {
	auto archive = archive_read_new();
				
	auto result = archive_read_support_filter_all(archive); // I don't see deflate so using all
	assert(result == ARCHIVE_OK);
				
	result = archive_read_support_format_zip(archive);
	assert(result == ARCHIVE_OK);
				
	result = archive_read_open_memory(archive, &input[0], input.size());
	assert(result == ARCHIVE_OK);
				
	archive_entry* entry;
	result = archive_read_next_header(archive, &entry);
	if (result != ARCHIVE_OK) {
		return "read next header error " + std::to_string(result);
	}
	assert(result == ARCHIVE_OK);

	std::string output;
	output.resize(archive_entry_size(entry));
	
	auto length = archive_read_data(archive, &output[0], output.size());
	assert(length == output.size());
	if (length != output.size()) {
		return "hello world" + std::to_string(length);
	}
				
	assert(archive_read_next_header(archive, &entry) == ARCHIVE_EOF);
				
	result = archive_read_free(archive);
	assert(result == ARCHIVE_OK);

	return output;
}

SteamClient::CMClient::CMClient(std::function<void(std::size_t, std::function<void(unsigned char*)>)> write) : write(std::move(write)) {
	steamID.instance = 1;
	steamID.universe = static_cast<unsigned>(EUniverse::Public);
	steamID.type = static_cast<unsigned>(EAccountType::Individual);
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
		auto crypted_size = crypted_length(length);
		
		write(8 + crypted_size, [&](unsigned char* out_buffer) {
			auto in_buffer = new unsigned char[length];
			fill(in_buffer);
			
			symmetric_encrypt(rnd, sessionKey, in_buffer, length, out_buffer + 8);
			
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
