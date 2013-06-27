#include <algorithm>
#include <cassert>

#include <cryptopp/rsa.h>

#include <zlib.h>

#include <archive.h>
#include <archive_entry.h>

#include "cmclient.h"

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

void SteamClient::HandleMessage(EMsg emsg, const unsigned char* data, std::size_t length, std::uint64_t job_id) {
	switch (emsg) {
	
	case EMsg::ChannelEncryptRequest:
		{
			auto enc_request = reinterpret_cast<const MsgChannelEncryptRequest*>(data);
			
			RSA::PublicKey key;
			ArraySource source(public_key, sizeof(public_key), true /* pumpAll */);
			key.Load(source);
			RSAES_OAEP_SHA_Encryptor rsa(key);
			
			auto rsa_size = rsa.FixedCiphertextLength();
			
			cmClient->WriteMessage(EMsg::ChannelEncryptResponse, sizeof(MsgChannelEncryptResponse) + rsa_size + 4 + 4, [this, &rsa, rsa_size](unsigned char* buffer) {
				auto enc_resp = new (buffer) MsgChannelEncryptResponse;
				auto crypted_sess_key = buffer + sizeof(MsgChannelEncryptResponse); 
				
				cmClient->rnd.GenerateBlock(cmClient->sessionKey, sizeof(cmClient->sessionKey));
				
				rsa.Encrypt(cmClient->rnd, cmClient->sessionKey, sizeof(cmClient->sessionKey), crypted_sess_key);
				
				auto crc = crc32(0, crypted_sess_key, rsa_size);
				
				*reinterpret_cast<std::uint32_t*>(crypted_sess_key + rsa_size) = crc;
				*reinterpret_cast<std::uint32_t*>(crypted_sess_key + rsa_size + 4) = 0;
			});
		}
		
		break;
		
	case EMsg::ChannelEncryptResult:
		{
			auto enc_result = reinterpret_cast<const MsgChannelEncryptResult*>(data);
			assert(enc_result->result == static_cast<std::uint32_t>(EResult::OK));
			
			cmClient->encrypted = true;
			
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
				onLogOn(eresult, cmClient->steamID);
			}
			
			if (eresult == EResult::OK) {
				setInterval([this] {
					cmClient->WriteMessage(EMsg::ClientHeartBeat, CMsgClientHeartBeat());
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
			
			byte sha[20];
			SHA1().CalculateDigest(sha, reinterpret_cast<const byte*>(bytes.data()), bytes.length());
			
			CMsgClientUpdateMachineAuthResponse response;
			response.set_sha_file(sha, 20);
			cmClient->WriteMessage(EMsg::ClientUpdateMachineAuthResponse, response, job_id);
			
			onSentry(sha);
		}
		
		break;
		
	case EMsg::ClientPersonaState:
		{
			if (!onUserInfo) {
				return;
			}
			
			CMsgClientPersonaState state;
			state.ParseFromArray(data, length);
			
			for (auto &user : state.friends()) {
				SteamID steamid_source = user.steamid_source();
				auto persona_state = user.persona_state();
				
				onUserInfo(
					user.friendid(),
					user.has_steamid_source() ? &steamid_source : nullptr,
					user.has_player_name() ? user.player_name().c_str() : nullptr,
					user.has_persona_state() ? reinterpret_cast<EPersonaState*>(&persona_state) : nullptr
				);
			}
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
			auto member_count = *reinterpret_cast<const std::uint32_t*>(data + sizeof(MsgClientChatEnter));
			auto chat_name = reinterpret_cast<const char*>(data + sizeof(MsgClientChatEnter) + 4);
			
			// fast-forward to the first byte after the name
			auto members = chat_name;
			while (*members++);
			
			onChatEnter(
				msg->steamIdChat,
				static_cast<EChatRoomEnterResponse>(msg->enterResponse),
				chat_name,
				member_count,
				reinterpret_cast<const ChatMember*>(members)
			);
		}
		
		break;
		
	case EMsg::ClientChatMemberInfo:
		{
			if (!onChatStateChange)
				return;
			
			auto member_info = reinterpret_cast<const MsgClientChatMemberInfo*>(data);
			
			if (static_cast<EChatInfoType>(member_info->type) != EChatInfoType::StateChange)
				return; // TODO
			
			auto payload = data + sizeof(MsgClientChatMemberInfo);
			
			auto acted_on = *reinterpret_cast<const SteamID*>(payload);
			auto state_change = *reinterpret_cast<const EChatMemberStateChange*>(payload + 8);
			auto acted_by = *reinterpret_cast<const SteamID*>(payload + 8 + 4);
			auto member = reinterpret_cast<const ChatMember*>(payload + 8 + 4 + 8);
			
			onChatStateChange(member_info->steamIdChat, acted_by, acted_on, state_change, member);
		}
		
		break;
		
	case EMsg::ClientFriendsList:
		{
			if (!onRelationships)
				return;
			
			CMsgClientFriendsList list;
			list.ParseFromArray(data, length);
			
			std::vector<SteamID> steamids;
			std::vector<EFriendRelationship> relationships;
			
			for (auto &user : list.friends()) {
				steamids.push_back(user.ulfriendid());
				relationships.push_back(static_cast<EFriendRelationship>(user.efriendrelationship()));
			}
			
			onRelationships(list.bincremental(), list.friends_size(), steamids.data(), relationships.data());
		}
		
		break;
		
	case EMsg::ClientFriendMsgIncoming:
		{
			if (!onPrivateMsg && !onTyping)
				return;
			
			CMsgClientFriendMsgIncoming msg;
			msg.ParseFromArray(data, length);
			
			switch (static_cast<EChatEntryType>(msg.chat_entry_type())) {
				
			case EChatEntryType::ChatMsg:
				if (onPrivateMsg)
					onPrivateMsg(msg.steamid_from(), msg.message().c_str());
				break;
				
			case EChatEntryType::Typing:
				if (onTyping)
					onTyping(msg.steamid_from());
				break;
				
			case EChatEntryType::LeftConversation:
				// the other party closed the window
				// not implemented by Steam client
				break;
				
			default:
				assert(!"Unexpected message type!");
			}
		}
		
		break;
	}
}
