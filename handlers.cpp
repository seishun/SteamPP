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

void SteamClient::HandleMessage(EMsg eMsg, const unsigned char* data, std::size_t length)
{
	switch (eMsg)
	{

	case EMsg::ChannelEncryptRequest:
		{
			auto encRequest = reinterpret_cast<const MsgChannelEncryptRequest*>(data);

			auto bio = BIO_new_mem_buf(public_key, sizeof(public_key));
			auto rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
			BIO_vfree(bio);

			auto rsa_size = RSA_size(rsa);

			WriteMessage(EMsg::ChannelEncryptResponse, false, sizeof(MsgChannelEncryptResponse) + rsa_size + 4 + 4, [this, &rsa, rsa_size](unsigned char* buffer)
			{
				auto encResp = new (buffer) MsgChannelEncryptResponse;
				auto cryptedSessKey = buffer + sizeof(MsgChannelEncryptResponse); 

				RAND_bytes(sessionKey, sizeof(sessionKey));

				RSA_public_encrypt
					(
					sizeof(sessionKey),    // flen
					sessionKey,            // from
					cryptedSessKey,        // to
					rsa,                   // rsa
					RSA_PKCS1_OAEP_PADDING // padding
					);

				auto crc = crc32(0, cryptedSessKey, rsa_size);

				*reinterpret_cast<std::uint32_t*>(cryptedSessKey + rsa_size) = crc;
				*reinterpret_cast<std::uint32_t*>(cryptedSessKey + rsa_size + 4) = 0;

				RSA_free(rsa);
			});

			break;
		}


	case EMsg::ChannelEncryptResult:
		{
			auto encResult = reinterpret_cast<const MsgChannelEncryptResult*>(data);
			assert(encResult->result == static_cast<std::uint32_t>(EResult::OK));

			encrypted = true;

			CMsgClientLogon logon;
			logon.set_account_name(username_);
			logon.set_password(password_);
			logon.set_protocol_version(65575);
			if (code_.length())
			{
				logon.set_auth_code(code_);
			}

			auto size = logon.ByteSize();
			WriteMessage(EMsg::ClientLogon, true, size, [&logon, size](unsigned char* buffer)
			{
				logon.SerializeToArray(buffer, size);
			});

			break;
		}


	case EMsg::Multi:
		{
			CMsgMulti msgMulti;
			msgMulti.ParseFromArray(data, length);
			auto size_unzipped = msgMulti.size_unzipped();
			auto payload = msgMulti.message_body();
			auto data = reinterpret_cast<const unsigned char*>(payload.data());

			if (size_unzipped > 0)
			{
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
			for (unsigned offset = 0; offset < payload_size;)
			{
				auto subSize = *reinterpret_cast<const std::uint32_t*>(data + offset);
				ReadMessage(data + offset + 4, subSize);
				offset += 4 + subSize;
			}

			if (size_unzipped > 0)
			{
				delete[] data;
			}

			break;
		}


	case EMsg::ClientLogOnResponse:
		{
			CMsgClientLogonResponse logon_resp;
			logon_resp.ParseFromArray(data, length);
			auto eresult = static_cast<EResult>(logon_resp.eresult());
			auto interval = logon_resp.out_of_game_heartbeat_seconds();
			
			if (eresult == EResult::OK) {
				if (onLogOn)
				{
					onLogOn();
				}

				set_interval_([this]()
				{
					CMsgClientHeartBeat heartbeat;
					auto size = heartbeat.ByteSize();
					WriteMessage(EMsg::ClientHeartBeat, true, size, [&heartbeat, size](unsigned char* buffer)
					{
						heartbeat.SerializeToArray(buffer, size);
					});
				}, interval);
			}


			break;
		}

	case EMsg::ClientChatMsg:
		{
			if (!onChatMsg)
				// no listener
				return;

			auto msg = reinterpret_cast<const MsgClientChatMsg*>(data);
			auto raw = reinterpret_cast<const char*>(data + sizeof(MsgClientChatMsg));

			// Steam cuts off after the first null or displays the whole string if there isn't one
			onChatMsg(msg->steamIdChatRoom, std::string(raw, strnlen(raw, length - sizeof(MsgClientChatMsg))));

			break;
		}
	}
}