#include <algorithm>
#include <cassert>

#include "steam++.h"
#include "steam_language/steam_language_internal.h"
#include "steammessages_clientserver.pb.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <zlib.h>

#include <archive.h>
#include <archive_entry.h>

char public_key[] =
	"-----BEGIN PUBLIC KEY-----\n"
	"MIGdMA0GCSqGSIb3DQEBAQUAA4GLADCBhwKBgQDf7BrWLBBmLBc1OhSwfFkRf53T\n"
	"2Ct64+AVzRkeRuh7h3SiGEYxqQMUeYKO6UWiSRKpI2hzic9pobFhRr3Bvr/WARvY\n"
	"gdTckPv+T1JzZsuVcNfFjrocejN1oWI0Rrtgt4Bo+hOneoo3S57G9F1fOpn5nsQ6\n"
	"6WOiu4gZKODnFMBCiQIBEQ==\n"
	"-----END PUBLIC KEY-----\n";

using namespace Steam;

void SteamClient::HandleMessage(EMsg emsg, const unsigned char* data, std::size_t length, std::uint64_t job_id) {
	switch (emsg) {
	
	case EMsg::ChannelEncryptRequest:
		{
			auto enc_request = reinterpret_cast<const MsgChannelEncryptRequest*>(data);
			
			auto bio = BIO_new_mem_buf(public_key, sizeof(public_key));
			auto rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
			BIO_vfree(bio);
			
			auto rsa_size = RSA_size(rsa);
			
			WriteMessage(EMsg::ChannelEncryptResponse, false, sizeof(MsgChannelEncryptResponse) + rsa_size + 4 + 4, [this, &rsa, rsa_size](unsigned char* buffer) {
				auto enc_resp = new (buffer) MsgChannelEncryptResponse;
				auto crypted_sess_key = buffer + sizeof(MsgChannelEncryptResponse); 
				
				RAND_bytes(sessionKey, sizeof(sessionKey));
				
				RSA_public_encrypt(
					sizeof(sessionKey),    // flen
					sessionKey,            // from
					crypted_sess_key,      // to
					rsa,                   // rsa
					RSA_PKCS1_OAEP_PADDING // padding
				);
				
				auto crc = crc32(0, crypted_sess_key, rsa_size);
				
				*reinterpret_cast<std::uint32_t*>(crypted_sess_key + rsa_size) = crc;
				*reinterpret_cast<std::uint32_t*>(crypted_sess_key + rsa_size + 4) = 0;
				
				RSA_free(rsa);
			});
		}
		
		break;
		
	case EMsg::ChannelEncryptResult:
		{
			auto enc_result = reinterpret_cast<const MsgChannelEncryptResult*>(data);
			assert(enc_result->result == static_cast<std::uint32_t>(EResult::OK));
			
			encrypted = true;
			
			if (onHandshake) {
				onHandshake();
			}
		}
		
		break;
		
	case EMsg::Multi:
		{
			CMsgMulti msg_multi;
			msg_multi.ParseFromArray(data, length);
			auto size_unzipped = msg_multi.size_unzipped();
			auto payload = msg_multi.message_body();
			auto data = reinterpret_cast<const unsigned char*>(payload.data());
			
			if (size_unzipped > 0) {
				auto buffer = new unsigned char[size_unzipped];
				auto archive = archive_read_new();
				
				auto result = archive_read_support_filter_all(archive); // I don't see deflate so using all
				assert(result == ARCHIVE_OK);
				
				result = archive_read_support_format_zip(archive);
				assert(result == ARCHIVE_OK);
				
				result = archive_read_open_memory(archive, const_cast<unsigned char*>(data), payload.size());
				assert(result == ARCHIVE_OK);
				
				archive_entry* entry;
				result = archive_read_next_header(archive, &entry);
				assert(result == ARCHIVE_OK);
				assert(archive_entry_pathname(entry) == std::string("z"));
				assert(archive_entry_size(entry) == size_unzipped);
				
				auto length = archive_read_data(archive, buffer, size_unzipped);
				assert(length == size_unzipped);
				
				assert(archive_read_next_header(archive, &entry) == ARCHIVE_EOF);
				
				result = archive_read_free(archive);
				assert(result == ARCHIVE_OK);
				
				data = buffer;
			}
			
			auto payload_size = size_unzipped ? size_unzipped : payload.size();
			for (unsigned offset = 0; offset < payload_size;) {
				auto subSize = *reinterpret_cast<const std::uint32_t*>(data + offset);
				ReadMessage(data + offset + 4, subSize);
				offset += 4 + subSize;
			}
			
			if (size_unzipped > 0) {
				delete[] data;
			}			
		}
		
		break;
		
	case EMsg::ClientLogOnResponse:
		{
			CMsgClientLogonResponse logon_resp;
			logon_resp.ParseFromArray(data, length);
			auto eresult = static_cast<EResult>(logon_resp.eresult());
			auto interval = logon_resp.out_of_game_heartbeat_seconds();
			
			if (onLogOn) {
				onLogOn(eresult, steamID);
			}
			
			if (eresult == EResult::OK) {
				setInterval([this] {
					CMsgClientHeartBeat heartbeat;
					auto size = heartbeat.ByteSize();
					WriteMessage(EMsg::ClientHeartBeat, true, size, [&heartbeat, size](unsigned char* buffer) {
						heartbeat.SerializeToArray(buffer, size);
					});
				}, interval);
			}			
		}
		
		break;
		
	case EMsg::ClientUpdateMachineAuth:
		{
			if (!onSentry) {
				return;
			}
			
			CMsgClientUpdateMachineAuth machine_auth;
			machine_auth.ParseFromArray(data, length);
			auto &bytes = machine_auth.bytes();
			
			auto sha = SHA1(reinterpret_cast<const unsigned char*>(&bytes[0]), bytes.length(), NULL);
			
			CMsgClientUpdateMachineAuthResponse response;
			response.set_sha_file(sha, 20);
			auto size = response.ByteSize();
			WriteMessage(EMsg::ClientUpdateMachineAuthResponse, true, size, [&response, size](unsigned char *buffer) {
				response.SerializeToArray(buffer, size);
			}, job_id);
			
			onSentry(sha);
		}
		
		break;
		
	case EMsg::ClientChatMsg:
		{
			if (!onChatMsg)
				// no listener
				return;
			
			auto msg = reinterpret_cast<const MsgClientChatMsg*>(data);
			auto begin = reinterpret_cast<const char*>(data + sizeof(MsgClientChatMsg));
			auto end = reinterpret_cast<const char*>(data + length);
			
			// Steam cuts off after the first null or displays the whole string if there isn't one
			onChatMsg(
				msg->steamIdChatRoom,
				msg->steamIdChatter,
				std::find(begin, end, '\0') == end ?
					// no null, someone is using a non-conforming implementation
					std::string(begin, end - begin).c_str() :
					// null-terminated already, no copy necessary
					begin
			);
		}
		
		break;
		
	case EMsg::ClientChatEnter:
		{
			if (!onChatEnter)
				return;
			
			auto msg = reinterpret_cast<const MsgClientChatEnter*>(data);
			onChatEnter(msg->steamIdChat, static_cast<EChatRoomEnterResponse>(msg->enterResponse));
			
			// TODO: parse the payload
		}
		
		break;
	}
}
