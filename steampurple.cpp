#include <cassert>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "steam++.h"

// unistd.h is broken in MinGW
#ifdef _WIN32
#include <win32/win32dep.h>
#else
#include <unistd.h>
#endif

#include <purple.h>

using namespace Steam;

struct SteamPurple {
	SteamClient client;
	
	int fd;
	std::vector<unsigned char> read_buffer;
	std::vector<unsigned char> write_buffer;
	std::vector<unsigned char>::size_type read_offset;
	guint watcher;
	PurpleProxyConnectData *connect_data;
	
	guint timer;
	std::function<void()> callback;
};

static gboolean plugin_load(PurplePlugin *plugin) {
	return TRUE;
}

static void steam_connect(PurpleAccount *account, SteamPurple* steam) {
	auto &endpoint = servers[rand() % (sizeof(servers) / sizeof(servers[0]))];
	steam->connect_data = purple_proxy_connect(NULL, account, endpoint.host, endpoint.port, [](gpointer data, gint source, const gchar *error_message) {
		auto pc = (PurpleConnection *)data;
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		if (source == -1) {
			steam->connect_data = NULL;
			purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error_message);
			return;
		}
		steam->fd = source;
		auto next_length = steam->client.connected();
		steam->read_buffer.resize(next_length);
		steam->watcher = purple_input_add(source, PURPLE_INPUT_READ, [](gpointer data, gint source, PurpleInputCondition cond) {
			auto pc = (PurpleConnection *)data;
			auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
			auto len = read(source, &steam->read_buffer[steam->read_offset], steam->read_buffer.size() - steam->read_offset);
			purple_debug_info("steam", "read: %i\n", len);
			// len == 0: preceded by a ClientLoggedOff or ClientLogOnResponse, socket should be already closed by us
			if (len == -1) {
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, strerror(errno));
				purple_input_remove(steam->watcher); // on Linux, steam_close is called too late and Pidgin catches the EOF
				steam->watcher = 0;
				return;
			}
			assert(len > 0);
			steam->read_offset += len;
			if (steam->read_offset == steam->read_buffer.size()) {
				auto next_len = steam->client.readable(steam->read_buffer.data());
				steam->read_offset = 0;
				steam->read_buffer.resize(next_len);
			}
		}, pc);
	}, purple_account_get_connection(account));
	assert(steam->connect_data);
}

static void steam_set_steam_guard_token_cb(gpointer data, const gchar *steam_guard_token) {
	auto pc = (PurpleConnection *)data;
	auto account = purple_connection_get_account(pc);
	auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
	
	purple_debug_info("steam", "Got token: %s\n", steam_guard_token);
	steam_connect(account, steam);
	
	std::string token(steam_guard_token);
	steam->client.onHandshake = [steam, account, token] {
		// we just copied the token twice (compiler optimizations notwithstanding) but this is the prettiest way to access it in the lambda
		// yay for capture by move in C++14
		steam->client.LogOn(purple_account_get_username(account), purple_account_get_password(account), nullptr, token.c_str());
	};
}

char icon_spec_format[] = "png,jpeg";

static PurplePluginProtocolInfo prpl_info = {
	#if PURPLE_VERSION_CHECK(3, 0, 0)
	sizeof(PurplePluginProtocolInfo),       /* struct_size */
	#endif
	
	/* options */
	(PurpleProtocolOptions)NULL,
	
	NULL,                   /* user_splits */
	NULL,                   /* protocol_options */
	/* NO_BUDDY_ICONS */    /* icon_spec */
	{icon_spec_format, 0, 0, 64, 64, 0, PURPLE_ICON_SCALE_DISPLAY}, /* icon_spec */
	
	/* list_icon */
	[](PurpleAccount *account, PurpleBuddy *buddy) {
		return "steam";
	},
	
	NULL, //steam_list_emblem,         /* list_emblems */
	
	/* status_text */
	[](PurpleBuddy *buddy) {
		// apparently, "char *" means "takes ownership" in libpurple's books
		return g_strdup((gchar *)purple_buddy_get_protocol_data(buddy));
	},
	
	NULL, //steam_tooltip_text,        /* tooltip_text */
	
	/* status_types */
	[](PurpleAccount *account) {
		GList *types = NULL;
		PurpleStatusType *status;
		
		purple_debug_info("steam", "status_types\n");
		
		status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, NULL, "Online", TRUE, TRUE, FALSE);
		types = g_list_append(types, status);
		status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, NULL, "Offline", TRUE, TRUE, FALSE);
		types = g_list_append(types, status);
		status = purple_status_type_new_full(PURPLE_STATUS_UNAVAILABLE, NULL, "Busy", TRUE, TRUE, FALSE);
		types = g_list_append(types, status);
		status = purple_status_type_new_full(PURPLE_STATUS_AWAY, NULL, "Away", TRUE, TRUE, FALSE);
		types = g_list_append(types, status);
		status = purple_status_type_new_full(PURPLE_STATUS_EXTENDED_AWAY, NULL, "Snoozing", TRUE, TRUE, FALSE);
		types = g_list_append(types, status);
		
		status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, "trade", "Looking to Trade", TRUE, FALSE, FALSE);
		types = g_list_append(types, status);
		status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, "play", "Looking to Play", TRUE, FALSE, FALSE);
		types = g_list_append(types, status);
		
		return types;
	},
	
	NULL, //steam_node_menu,           /* blist_node_menu */
	
	/* chat_info */
	[](PurpleConnection *gc) {
		return g_list_append(NULL, new proto_chat_entry {
			/* label */      "SteamID",
			/* identifier */ "steamID",
			/* required */   TRUE
		});
	},
	
	NULL,//steam_chat_info_defaults,  /* chat_info_defaults */
	
	/* login */
	[](PurpleAccount *account) {
		auto pc = purple_account_get_connection(account);
		auto steam = new SteamPurple {
			// SteamClient constructor
			{
				// why use pc instead of steam directly?
				// we can't take steam by value because the pointer is uninitialized at this point
				// we can't take steam by reference because it'll go out of scope when steam_login returns
				
				// write callback
				// we don't actually need account below. it's a workaround for #54947
				// TODO: remove when Ubuntu ships with a newer GCC
				[account, pc](std::size_t length, std::function<void(unsigned char* buffer)> fill) {
					// TODO: check if previous write has finished
					auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
					steam->write_buffer.resize(length);
					fill(steam->write_buffer.data());
					auto len = write(steam->fd, steam->write_buffer.data(), steam->write_buffer.size());
					assert(len == steam->write_buffer.size());
					// TODO: check len
				},
				
				// set_interval callback
				// same as above
				[account, pc](std::function<void()> callback, int timeout) {
					auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
					steam->callback = std::move(callback);
					steam->timer = purple_timeout_add_seconds(timeout, [](gpointer user_data) -> gboolean {
						auto steam = reinterpret_cast<SteamPurple*>(user_data);
						steam->callback();
						return TRUE;
					}, steam);
				}
			}
			// value-initialize the rest
		};
		
		purple_connection_set_protocol_data(pc, steam);
		
		steam->client.onHandshake = [steam, account] {
			auto base64 = purple_account_get_string(account, "sentry_hash", nullptr);
			unsigned char* hash = nullptr;
			
			if (base64)
				hash = purple_base64_decode(base64, NULL);
			
			SteamID steamID;
			auto steamID_string = purple_account_get_string(account, "steamid", nullptr);
			if (steamID_string && purple_account_get_bool(account, "console", FALSE)) {
				steamID = g_ascii_strtoull(steamID_string, NULL, 10);
				steamID.instance = 2;
			}
			
			steam->client.LogOn(purple_account_get_username(account), purple_account_get_password(account), hash, nullptr, steamID);
			
			if (base64)
				g_free(hash);
		};
		
		steam->client.onLogOn = [account, pc, steam](EResult result, SteamID steamID) {
			auto steamID_string = std::to_string(steamID);
			
			switch (result) {
			case EResult::OK:
				steam->client.SetPersonaState(EPersonaState::Online);
				purple_connection_set_state(pc, PURPLE_CONNECTED);
				purple_connection_set_display_name(pc, steamID_string.c_str());
				purple_account_set_string(account, "steamid", steamID_string.c_str());
				return;
			case EResult::AccountLogonDenied:
				purple_request_input(
					/* handle */        NULL,
					/* title */         NULL,
					/* primary */       "Set your Steam Guard Code",
					/* secondary */     "Copy the Steam Guard Code you will have received in your email",
					/* default_value */ NULL,
					/* multiline */     FALSE,
					/* masked */        FALSE,
					/* hint */          "Steam Guard Code",
					/* ok_text */       "OK",
					/* ok_cb */         G_CALLBACK(steam_set_steam_guard_token_cb),
					/* cancel_text */   "Cancel",
					/* cancel_cb */     NULL,
					/* account */       account,
					/* who */           NULL,
					/* conv */          NULL,
					/* user_data */     pc
				);
				// preemptively close the socket because we don't want Pidgin to display a disconnected message
				close(steam->fd);
				purple_input_remove(steam->watcher);
				break;
			case EResult::AlreadyLoggedInElsewhere:
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Already logged in elsewhere");
				break;
			case EResult::InvalidPassword:
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED, "Invalid password");
				break;
			case EResult::ServiceUnavailable:
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Steam is down");
				break;
			case EResult::TryAnotherCM:
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "This server is down");
				break;
			default:
				purple_debug_error("steam", "Unknown logon eresult: %i\n", result);
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Unknown error");
			}
			
			purple_input_remove(steam->watcher); // on Linux, steam_close is called too late and Pidgin catches the EOF
			steam->watcher = 0;
		};
		
		steam->client.onLogOff = [pc, steam](EResult result) {
			switch (result) {
			case EResult::LoggedInElsewhere:
			case EResult::LogonSessionReplaced:
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Logged in elsewhere");
				break;
			case EResult::ServiceUnavailable:
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Steam went down");
				break;
			default:
				purple_debug_error("steam", "Unknown logoff eresult: %i\n", result);
				purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Unknown error");
			}
			
			purple_input_remove(steam->watcher); // on Linux, steam_close is called too late and Pidgin catches the EOF
			steam->watcher = 0;
		};
		
		steam->client.onSentry = [account](const unsigned char hash[20]) {
			auto base64 = purple_base64_encode(hash, 20);
			purple_account_set_string(account, "sentry_hash", base64);
			g_free(base64);
		};
		
		steam->client.onUserInfo = [pc, account](
			SteamID user,
			SteamID* source,
			const char* name,
			EPersonaState* state,
			const unsigned char avatar_hash[20],
			const char* game_name
		) {
			auto user_string = std::to_string(user);
			
			if (source && static_cast<EAccountType>(source->type) == EAccountType::Chat) {
				// either we're joining a chat or something is happening in a chat
				
				// create a dummy group to store aliases if it doesn't exist yet
				auto group = purple_group_new(std::to_string(*source).c_str());
				
				if (!purple_find_buddy_in_group(account, user_string.c_str(), group)) {
					// someone new to this chat
					purple_blist_add_buddy(purple_buddy_new(account, user_string.c_str(), NULL), NULL, group, NULL);
				}
			}
			
			if (name) {
				serv_got_alias(pc, user_string.c_str(), name);
				if (user == g_ascii_strtoull(purple_connection_get_display_name(pc), NULL, 10))
					purple_account_set_alias(account, name);
			}
			
			if (state) {
				PurpleStatusPrimitive prim;
				switch (*state) {
				case EPersonaState::Offline:
					prim = PURPLE_STATUS_OFFLINE;
					break;
				// these would look the same in Pidgin anyway
				case EPersonaState::Online:
				case EPersonaState::LookingToTrade:
				case EPersonaState::LookingToPlay:
					prim = PURPLE_STATUS_AVAILABLE;
					break;
				case EPersonaState::Busy:
					prim = PURPLE_STATUS_UNAVAILABLE;
					break;
				case EPersonaState::Away:
					prim = PURPLE_STATUS_AWAY;
					break;
				case EPersonaState::Snooze:
					prim = PURPLE_STATUS_EXTENDED_AWAY;
					break;
				}
				purple_prpl_got_user_status(account, user_string.c_str(), purple_primitive_get_id_from_type(prim), NULL);
			}
			
			if (game_name) {
				auto buddies = purple_find_buddies(account, user_string.c_str());
				if (!buddies) {
					purple_debug_info("steam", "buddy %s not found\n", user_string.c_str());
					return;
				}
				
				auto html = g_markup_escape_text(game_name, -1);
				
				g_slist_foreach(buddies, [](gpointer data, gpointer user_data) {
					auto buddy = PURPLE_BUDDY(data);
					auto HTML = reinterpret_cast<decltype(html)>(user_data);
					
					g_free(purple_buddy_get_protocol_data(buddy));
					// empty game_name means not in-game
					purple_buddy_set_protocol_data(buddy, std::strlen(HTML) ? g_strconcat("In-Game: ", HTML, NULL) : NULL);
				}, html);
				
				g_free(html);
			}
			
			if (avatar_hash) {
				// is this really the simplest way to get a hex string? shame on you, C++
				std::ostringstream ss;
				ss << std::hex << std::setfill('0');
				for (auto i = 0; i < 20; i++)
					ss << std::setw(2) << static_cast<unsigned>(avatar_hash[i]);
				auto new_avatar = ss.str();
				
				purple_debug_info("steam", "%s has avatar %s\n", user_string.c_str(), new_avatar.c_str());
				
				auto buddy = purple_find_buddy(account, user_string.c_str());
				if (!buddy) {
					purple_debug_info("steam", "buddy %s not found\n", user_string.c_str());
					return;
				}
				
				auto icon = purple_buddy_icons_find(account, user_string.c_str());
				if (icon) {
					auto old_avatar = purple_buddy_icon_get_checksum(icon);
					purple_debug_info("steam", "old avatar was: %s\n", old_avatar);
					if (old_avatar == new_avatar)
						return;
				}
				
				purple_util_fetch_url(
					("http://media.steampowered.com/steamcommunity/public/images/avatars/" + new_avatar.substr(0, 2) + "/" + new_avatar + ".jpg").c_str(),
					TRUE,
					NULL,
					FALSE,
					[](PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message) {
						auto buddy = PURPLE_BUDDY(user_data);
						// can't be arsed to pass both the buddy and the hash, so just recalculate the latter
						auto hash = purple_util_get_image_checksum(url_text, len);
						purple_buddy_icons_set_for_user(purple_buddy_get_account(buddy), purple_buddy_get_name(buddy), g_memdup(url_text, len), len, hash);
					},
					buddy
				);
			}
		};
		
		steam->client.onChatEnter = [pc](
			SteamID room,
			EChatRoomEnterResponse response,
			const char* name,
			std::size_t member_count,
			const ChatMember members[]
		) {
			if (response == EChatRoomEnterResponse::Success) {
				auto convo = serv_got_joined_chat(pc, room.ID, std::to_string(room).c_str());
				
				purple_conversation_set_title(convo, name);
				auto chat = purple_conversation_get_chat_data(convo);
				
				while (member_count--) {
					purple_conv_chat_add_user(
						chat,
						std::to_string(members[member_count].steamID).c_str(),
						NULL,
						PURPLE_CBFLAGS_NONE, // TODO
						FALSE
					);
				}
			} else {
				// TODO
			}
		};
		
		steam->client.onChatStateChange = [account, pc](
			SteamID room,
			SteamID acted_by,
			SteamID acted_on,
			EChatMemberStateChange state_change,
			const ChatMember* member
		) {
			auto convo = purple_find_chat(pc, room.ID);
			auto chat = purple_conversation_get_chat_data(convo);
			auto acted_on_string = std::to_string(acted_on);
			
			if (state_change == EChatMemberStateChange::Entered) {
				purple_conv_chat_add_user(chat, acted_on_string.c_str(), NULL, PURPLE_CBFLAGS_NONE, TRUE);
			} else {
				// TODO: print reason
				if (acted_on == g_ascii_strtoull(purple_connection_get_display_name(pc), NULL, 10)) {
					// we got kicked or banned
					serv_got_chat_left(pc, room.ID);
				} else {
					purple_conv_chat_remove_user(chat, acted_on_string.c_str(), NULL);
					
					// remove the respective buddy
					auto group_buddy = purple_find_buddy_in_group(
						account,
						acted_on_string.c_str(),
						purple_find_group(purple_conversation_get_name(convo))
					);
					purple_blist_remove_buddy(group_buddy);
				}
			}
		};
		
		steam->client.onChatMsg = [pc](SteamID room, SteamID chatter, const char* message) {
			auto html = purple_markup_escape_text(message, -1);
			serv_got_chat_in(pc, room.ID, std::to_string(chatter).c_str(), PURPLE_MESSAGE_RECV, html, time(NULL));
			g_free(html);
		};
		
		steam->client.onPrivateMsg = [pc](SteamID user, const char* message) {
			auto html = purple_markup_escape_text(message, -1);
			serv_got_im(pc, std::to_string(user).c_str(), html, PURPLE_MESSAGE_RECV, time(NULL));
			g_free(html);
		};
		
		steam->client.onTyping = [pc](SteamID user) {
			serv_got_typing(pc, std::to_string(user).c_str(), 20, PURPLE_TYPING);
		};
		
		steam->client.onRelationships = [account, steam](
			bool incremental,
			std::map<SteamID, EFriendRelationship> &users,
			std::map<SteamID, EClanRelationship> &groups
		) {
			if (!incremental) {
				// remove buddies who are no longer friends
				auto buddies = purple_find_buddies(account, NULL);
				g_slist_foreach(buddies, [](gpointer data, gpointer user_data) {
					auto &userz = *reinterpret_cast<decltype(&users)>(user_data);
					auto buddy = PURPLE_BUDDY(data);
					if (userz[g_ascii_strtoull(purple_buddy_get_name(buddy), NULL, 10)] != EFriendRelationship::Friend) {
						purple_blist_remove_buddy(buddy);
					}
				}, &users);
				g_slist_free(buddies);
			}
			
			std::vector<SteamID> friends;
			
			for (auto &relationship : users) {
				auto &user = relationship.first;
				auto user_string = std::to_string(user);
				
				switch (relationship.second) {
				case EFriendRelationship::None:
					purple_blist_remove_buddy(purple_find_buddy(account, user_string.c_str()));
					break;
				case EFriendRelationship::RequestRecipient:
					// TODO
					purple_debug_info("steam", "RequestRecipient not implemented\n");
					break;
				case EFriendRelationship::Friend:
					if (!incremental)
						friends.push_back(user);
					if (!purple_find_buddy(account, user_string.c_str()))
						purple_blist_add_buddy(purple_buddy_new(account, user_string.c_str(), NULL), NULL, NULL, NULL);
					break;
				case EFriendRelationship::RequestInitiator:
					// TODO
					purple_debug_info("steam", "RequestInitiator not implemented\n");
					break;
				default:
					// TODO
					purple_debug_info("steam", "EFriendRelationship not implemented: %i\n", relationship.second);
				}
			}
			
			if (!incremental) {
				// request info because we'll only get it for online friends
				steam->client.RequestUserInfo(friends.size(), friends.data());
			}
		};
		
		steam_connect(account, steam);
	},	
	
	/* close */
	[](PurpleConnection *pc) {
		purple_debug_info("steam", "Closing...\n");
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		if (steam->fd) {
			close(steam->fd);
			if (steam->watcher)
				purple_input_remove(steam->watcher);
			if (steam->timer)
				purple_timeout_remove(steam->timer);
		} else if (steam->connect_data) {
			purple_proxy_connect_cancel(steam->connect_data);
		}
		delete steam;
	},
	
	/* send_im */
	[](PurpleConnection *pc, const char *who, const char *message, PurpleMessageFlags flags) {
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		auto stripped = purple_unescape_html(message);
		steam->client.SendPrivateMessage(g_ascii_strtoull(who, NULL, 10), stripped);
		g_free(stripped);
		return 1;
	},
	
	NULL,                      /* set_info */
	
	/* send_typing */
	[](PurpleConnection *pc, const gchar *name, PurpleTypingState state) {
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		if (state == PURPLE_TYPING) {
			steam->client.SendTyping(g_ascii_strtoull(name, NULL, 10));
		}
		return 20U;
	},
	
	NULL,//steam_get_info,            /* get_info */
	
	/* set_status */
	[](PurpleAccount *account, PurpleStatus *status) {
		auto pc = purple_account_get_connection(account);
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		
		auto prim = purple_status_type_get_primitive(purple_status_get_type(status));
		EPersonaState state;
		
		switch (prim) {
		case PURPLE_STATUS_AVAILABLE:
			state = EPersonaState::Online;
			break;
		case PURPLE_STATUS_UNAVAILABLE:
			state = EPersonaState::Busy;
			break;
		case PURPLE_STATUS_AWAY:
			state = EPersonaState::Away;
			break;
		case PURPLE_STATUS_EXTENDED_AWAY:
			state = EPersonaState::Snooze;
			break;
		case PURPLE_STATUS_INVISIBLE:
			state = EPersonaState::Offline;
			break;
		}
		steam->client.SetPersonaState(state);
	},
	
	NULL, //steam_set_idle,            /* set_idle */
	NULL,                   /* change_passwd */
	NULL, //steam_add_buddy,           /* add_buddy */
	NULL,                   /* add_buddies */
	NULL, //steam_buddy_remove,        /* remove_buddy */
	NULL,                   /* remove_buddies */
	NULL,                   /* add_permit */
	NULL,                   /* add_deny */
	NULL,                   /* rem_permit */
	NULL,                   /* rem_deny */
	NULL,                   /* set_permit_deny */
	
	/* join_chat */
	[](PurpleConnection *pc, GHashTable *components) {
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		auto steamID_string = (const gchar *)g_hash_table_lookup(components, "steamID");
		steam->client.JoinChat(g_ascii_strtoull(steamID_string, NULL, 10));
	},
	
	NULL,                   /* reject chat invite */
	NULL,//steam_get_chat_name,       /* get_chat_name */
	NULL,                   /* chat_invite */
	
	/* chat_leave */
	[](PurpleConnection *pc, int id) {
		auto chat = purple_find_chat(pc, id);
		auto chat_name = purple_conversation_get_name(chat);
		
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		steam->client.LeaveChat(g_ascii_strtoull(chat_name, NULL, 10));
		
		// clear the alias storage group
		// despite what the docs imply, you can't remove a non-empty group
		// you can't get the list of buddies in a group either
		
		auto users = purple_conv_chat_get_users(purple_conversation_get_chat_data(chat));
		
		g_list_foreach(users, [](gpointer data, gpointer user_data) {
			auto chat_buddy = (PurpleConvChatBuddy *)data;
			auto chat = (PurpleConversation *)user_data;
			auto group_buddy = purple_find_buddy_in_group(
				purple_conversation_get_account(chat),
				purple_conv_chat_cb_get_name(chat_buddy),
				purple_find_group(purple_conversation_get_name(chat))
			);
			purple_blist_remove_buddy(group_buddy);
		}, chat);
		
		purple_blist_remove_group(purple_find_group(chat_name));
	},
	
	NULL,                   /* chat_whisper */
	
	/* chat_send */
	[](PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags) {
		auto steam = reinterpret_cast<SteamPurple*>(purple_connection_get_protocol_data(pc));
		auto stripped = purple_unescape_html(message);
		
		// can't reliably reconstruct a full SteamID from an account ID
		steam->client.SendChatMessage(g_ascii_strtoull(purple_conversation_get_name(purple_find_chat(pc, id)), NULL, 10), stripped);
		
		// the message doesn't get echoed automatically
		serv_got_chat_in(pc, id, purple_connection_get_display_name(pc), PURPLE_MESSAGE_SEND, message, time(NULL));
		
		g_free(stripped);
		return 1;
	},
	
	NULL,                   /* keepalive */
	NULL,                   /* register_user */
	NULL,                   /* get_cb_info */
	#if !PURPLE_VERSION_CHECK(3, 0, 0)
	NULL,                   /* get_cb_away */
	#endif
	NULL,                   /* alias_buddy */
	NULL,//steam_fake_group_buddy,    /* group_buddy */
	NULL,//steam_group_rename,        /* rename_group */
	
	/* buddy_free */
	[](PurpleBuddy *buddy) {
		g_free(purple_buddy_get_protocol_data(buddy));
	},

	NULL,//steam_conversation_closed, /* convo_closed */
	NULL,//purple_normalize_nocase,/* normalize */
	NULL,                   /* set_buddy_icon */
	NULL,//steam_group_remove,        /* remove_group */
	NULL,                   /* get_cb_real_name */
	NULL,                   /* set_chat_topic */
	NULL,                   /* find_blist_chat */
	NULL,                   /* roomlist_get_list */
	NULL,                   /* roomlist_cancel */
	NULL,                   /* roomlist_expand_category */
	NULL,                   /* can_receive_file */
	NULL,                   /* send_file */
	NULL,                   /* new_xfer */
	NULL,                   /* offline_message */
	NULL,                   /* whiteboard_prpl_ops */
	NULL,                   /* send_raw */
	NULL,                   /* roomlist_room_serialize */
	NULL,                   /* unregister_user */
	NULL,                   /* send_attention */
	NULL,                   /* attention_types */
	#if (PURPLE_MAJOR_VERSION == 2 && PURPLE_MINOR_VERSION >= 5) || PURPLE_MAJOR_VERSION > 2
	#if PURPLE_MAJOR_VERSION == 2 && PURPLE_MINOR_VERSION >= 5
	sizeof(PurplePluginProtocolInfo), /* struct_size */
	#endif
	NULL, /* steam_get_account_text_table, /* get_account_text_table */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
	#else
	(gpointer) sizeof(PurplePluginProtocolInfo)
	#endif
};

char id[] = "prpl-seishun-steam";
char name[] = "Steam";
char version[] = "1.0";
char summary[] = "";
char description[] = "";
char author[] = "Nicholas <vvnicholas@gmail.com>";
char homepage[] = "https://github.com/seishun/SteamPP";

static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_PROTOCOL,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,
	
	id,
	name,
	version,
	
	summary,
	description,
	author,
	homepage,
	
	plugin_load,
	NULL,
	NULL,
	
	NULL,
	&prpl_info,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static void init_plugin(PurplePlugin *plugin) {
	auto &options = prpl_info.protocol_options;
	
	options = g_list_append(options, purple_account_option_string_new(
		"SteamID (auto-filled and only needed for console instance)",
		"steamid",
		""
	));
	
	options = g_list_append(options, purple_account_option_bool_new(
		"Use console instance (requires SteamID and doesn't kick off Steam client)",
		"console",
		FALSE
	));
}

extern "C" {
	PURPLE_INIT_PLUGIN(steam, init_plugin, info)
}
