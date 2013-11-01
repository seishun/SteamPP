#include <algorithm>
#include <cassert>

#include <cryptopp/crc.h>

#include "cmclient.h"

void SteamClient::HandleMessage(EMsg emsg, const unsigned char* data, std::size_t length, std::uint64_t job_id) {
	switch (emsg) {
	
	case EMsg::ChannelEncryptRequest:
		{
			auto enc_request = reinterpret_cast<const MsgChannelEncryptRequest*>(data);
			
			cmClient->WriteMessage(EMsg::ChannelEncryptResponse, sizeof(MsgChannelEncryptResponse) + 128 + 4 + 4, [this](unsigned char* buffer) {
				auto enc_resp = new (buffer) MsgChannelEncryptResponse;
				auto crypted_sess_key = buffer + sizeof(MsgChannelEncryptResponse);

				create_session_key(cmClient->rnd, cmClient->sessionKey, crypted_sess_key);
				
				CRC32().CalculateDigest(crypted_sess_key + 128, crypted_sess_key, 128);
				*reinterpret_cast<std::uint32_t*>(crypted_sess_key + 128 + 4) = 0;
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
			
			if (size_unzipped > 0) {
				payload = unzip(payload);
				assert(payload.size() == size_unzipped);
			}
			
			auto data = reinterpret_cast<const unsigned char*>(payload.data());
			for (unsigned offset = 0; offset < payload.size();) {
				auto subSize = *reinterpret_cast<const std::uint32_t*>(data + offset);
				if (subSize == 0) {
					auto a = 5;
				}
				ReadMessage(data + offset + 4, subSize);
				offset += 4 + subSize;
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
		
	case EMsg::ClientLoggedOff:
		{
			if (!onLogOff) {
				return;
			}
			
			CMsgClientLoggedOff logged_off;
			logged_off.ParseFromArray(data, length);
			onLogOff(static_cast<EResult>(logged_off.eresult()));
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
					user.has_persona_state() ? reinterpret_cast<EPersonaState*>(&persona_state) : nullptr,
					user.has_avatar_hash() ? reinterpret_cast<const unsigned char*>(user.avatar_hash().data()) : nullptr,
					user.has_game_name() ? user.game_name().c_str() : nullptr
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
			
			std::map<SteamID, EFriendRelationship> users;
			std::map<SteamID, EClanRelationship> groups;
			
			for (auto &relationship : list.friends()) {
				SteamID steamID = relationship.ulfriendid();
				if (static_cast<EAccountType>(steamID.type) == EAccountType::Clan) {
					groups[steamID] = static_cast<EClanRelationship>(relationship.efriendrelationship());
				} else {
					users[steamID] = static_cast<EFriendRelationship>(relationship.efriendrelationship());
				}
			}
			
			onRelationships(list.bincremental(), users, groups);
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

	case EMsg::ClientGetAppOwnershipTicketResponse:
		{
			if (!onAppOwnershipTicket)
				return;

			CMsgClientGetAppOwnershipTicketResponse ticket_response;
			ticket_response.ParseFromArray(data, length);
			
			onAppOwnershipTicket(
				static_cast<EResult>(ticket_response.eresult()),
				ticket_response.app_id(),
				*ticket_response.mutable_ticket()
			);
		}

		break;

	case EMsg::PICSProductInfoResponse:
		{
			if (!onPICSProductInfo)
				return;

			CMsgPICSProductInfoResponse product_response;
			product_response.ParseFromArray(data, length);
			for (auto &app_info : *product_response.mutable_apps()) {
				auto &vdf = *app_info.mutable_buffer();
				vdf.pop_back(); // Valve thinks it's a good idea to append a null char to everything
				onPICSProductInfo(app_info.appid(), vdf);
			}
		}
		
		break;

	case EMsg::ClientGetDepotDecryptionKeyResponse:
		{
			if (!onDepotKey)
				return;

			CMsgClientGetDepotDecryptionKeyResponse key_response;
			key_response.ParseFromArray(data, length);
			
			onDepotKey(static_cast<EResult>(key_response.eresult()), key_response.depot_id(), *key_response.mutable_depot_encryption_key());
		}

		break;
	}
}
