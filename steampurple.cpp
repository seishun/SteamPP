#include <cassert>
#include <string>
#include <vector>

#include <unistd.h>

#define PURPLE_PLUGINS

#include <glib.h>

#include "debug.h"
#include "notify.h"
#include "plugin.h"
#include "version.h"

#include "steam++.h"

using namespace Steam;

struct SteamPurple {
	SteamClient client;
	
	int fd;
	std::vector<unsigned char> read_buffer;
	std::vector<unsigned char> write_buffer;
	std::vector<unsigned char>::size_type read_offset;
	
	guint timer;
	std::function<void()> callback;
};

static gboolean plugin_load(PurplePlugin* plugin) {
	return TRUE;
}

static const char* steam_list_icon(PurpleAccount* account, PurpleBuddy* buddy) {
	return "steam";
}

GList* steam_status_types(PurpleAccount* account) {
	GList* types = NULL;
	PurpleStatusType* status;
	
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
}

static void connect(PurpleAccount* account, SteamPurple* steam) {
	auto &endpoint = servers[rand() % (sizeof(servers) / sizeof(servers[0]))];
	purple_proxy_connect(NULL, account, endpoint.host, endpoint.port, [](gpointer data, gint source, const gchar* error_message) {
		// TODO: check source and error
		assert(source != -1);
		auto steam = reinterpret_cast<SteamPurple*>(data);
		steam->fd = source;
		auto next_length = steam->client.connected();
		steam->read_buffer.resize(next_length);
		purple_input_add(source, PURPLE_INPUT_READ, [](gpointer data, gint source, PurpleInputCondition cond) {
			auto steam = reinterpret_cast<SteamPurple*>(data);
			auto len = read(source, &steam->read_buffer[steam->read_offset], steam->read_buffer.size() - steam->read_offset);
			purple_debug_info("steam", "read: %i\n", len);
			assert(len != -1);
			// len == 0: preceded by a ClientLoggedOff or ClientLogOnResponse, i.e. already handled
			// len == -1: TODO
			steam->read_offset += len;
			if (steam->read_offset == steam->read_buffer.size()) {
				auto next_len = steam->client.readable(steam->read_buffer.data());
				steam->read_offset = 0;
				steam->read_buffer.resize(next_len);
			}
		}, steam);
	}, steam);
}

static void steam_login(PurpleAccount* account) {
	PurpleConnection* pc = purple_account_get_connection(account);
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
				auto steam = reinterpret_cast<SteamPurple*>(pc->proto_data);
				steam->write_buffer.resize(length);
				fill(steam->write_buffer.data());
				auto len = write(steam->fd, steam->write_buffer.data(), steam->write_buffer.size());
				assert(len == steam->write_buffer.size());
				// TODO: check len
			},
			
			// set_interval callback
			// same as above
			[account, pc](std::function<void()> callback, int timeout) {
				auto steam = reinterpret_cast<SteamPurple*>(pc->proto_data);
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
	
	pc->proto_data = steam;
	
	steam->client.onHandshake = [steam, account] {
		steam->client.LogOn(purple_account_get_username(account), purple_account_get_password(account));
	};
	
	steam->client.onLogOn = [pc, steam](EResult result) {
		switch (result) {
		case EResult::OK:
			steam->client.SetPersonaState(EPersonaState::Online);
			purple_connection_set_state(pc, PURPLE_CONNECTED);
			// TODO: use something meaningful like a steamid
			purple_connection_set_display_name(pc, "You");
			break;
		case EResult::AccountLogonDenied:
			purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED, "This account uses Steam Guard which is not supported yet");
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
			purple_debug_error("steam", "Unknown eresult: %i\n", result);
			purple_connection_error_reason(pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Unknown error");
		}
	};
	
	steam->client.onChatEnter = [pc](SteamID room, EChatRoomEnterResponse response) {
		if (response == EChatRoomEnterResponse::Success) {
			serv_got_joined_chat(pc, room.ID, std::to_string(room.steamID64).c_str());
		} else {
			// TODO
		}
	};
	
	steam->client.onChatMsg = [pc](SteamID room, SteamID chatter, std::string message) {
		serv_got_chat_in(pc, room.ID, std::to_string(chatter.steamID64).c_str(), PURPLE_MESSAGE_RECV, message.c_str(), time(NULL));
	};
	
	connect(account, steam);
}

static void steam_close(PurpleConnection* pc) {
	// TODO: actually log off maybe
	purple_debug_info("steam", "Closing...\n");
	auto steam = reinterpret_cast<SteamPurple*>(pc->proto_data);
	close(steam->fd);
	purple_timeout_remove(steam->timer);
	delete steam;
}

static GList* steam_chat_info(PurpleConnection* gc) {
	GList* m = NULL;
	struct proto_chat_entry* pce;
	
	pce = g_new0(struct proto_chat_entry, 1);
	pce->label = "SteamID";
	pce->identifier = "steamID";
	pce->required = TRUE;
	m = g_list_append(m, pce);
	
	return m;
}

void steam_join_chat(PurpleConnection* pc, GHashTable* components) {
	auto steam = reinterpret_cast<SteamPurple*>(pc->proto_data);
	auto steamID_string = reinterpret_cast<const gchar*>(g_hash_table_lookup(components, "steamID"));
	steam->client.JoinChat(atoll(steamID_string));
}

void steam_chat_leave(PurpleConnection* pc, int id) {
	auto steam = reinterpret_cast<SteamPurple*>(pc->proto_data);
	steam->client.LeaveChat(atoll(purple_conversation_get_name(purple_find_chat(pc, id))));
}

int steam_chat_send(PurpleConnection* pc, int id, const char* message, PurpleMessageFlags flags) {
	SteamPurple* steam = (SteamPurple* )pc->proto_data;
	
	// can't reliably reconstruct a full SteamID from an account ID
	steam->client.SendChatMessage(atoll(purple_conversation_get_name(purple_find_chat(pc, id))), message);
	
	// the message doesn't get echoed automatically
	serv_got_chat_in(pc, id, purple_connection_get_display_name(pc), PURPLE_MESSAGE_SEND, message, time(NULL));
	
	return 1;
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
	steam_list_icon,           /* list_icon */
	NULL, //steam_list_emblem,         /* list_emblems */
	NULL, //steam_status_text,         /* status_text */
	NULL, //steam_tooltip_text,        /* tooltip_text */
	steam_status_types,        /* status_types */
	NULL, //steam_node_menu,           /* blist_node_menu */
	steam_chat_info,           /* chat_info */
	NULL,//steam_chat_info_defaults,  /* chat_info_defaults */
	steam_login,               /* login */
	steam_close,               /* close */
	NULL, //steam_send_im,             /* send_im */
	NULL,                      /* set_info */
	NULL, //steam_send_typing,         /* send_typing */
	NULL,//steam_get_info,            /* get_info */
	NULL, //steam_set_status,          /* set_status */
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
	steam_join_chat,        /* join_chat */
	NULL,                   /* reject chat invite */
	NULL,//steam_get_chat_name,       /* get_chat_name */
	NULL,                   /* chat_invite */
	steam_chat_leave,       /* chat_leave */
	NULL,                   /* chat_whisper */
	steam_chat_send,        /* chat_send */
	NULL,                   /* keepalive */
	NULL,                   /* register_user */
	NULL,                   /* get_cb_info */
	#if !PURPLE_VERSION_CHECK(3, 0, 0)
	NULL,                   /* get_cb_away */
	#endif
	NULL,                   /* alias_buddy */
	NULL,//steam_fake_group_buddy,    /* group_buddy */
	NULL,//steam_group_rename,        /* rename_group */
	NULL,//steam_buddy_free,          /* buddy_free */
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

static void init_plugin(PurplePlugin* plugin) { }

extern "C" {
	PURPLE_INIT_PLUGIN(steam, init_plugin, info)
}
