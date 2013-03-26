#include <cstdint>
#pragma pack(push, 1)

namespace Steam
{
	struct UdpHeader
	{
		static const std::uint32_t MAGIC = 0x31305356;
		// Static size: 4
		std::uint32_t magic;
		// Static size: 2
		std::uint16_t payloadSize;
		// Static size: 1
		std::uint8_t packetType;
		// Static size: 1
		std::uint8_t flags;
		// Static size: 4
		std::uint32_t sourceConnID;
		// Static size: 4
		std::uint32_t destConnID;
		// Static size: 4
		std::uint32_t seqThis;
		// Static size: 4
		std::uint32_t seqAck;
		// Static size: 4
		std::uint32_t packetsInMsg;
		// Static size: 4
		std::uint32_t msgStartSeq;
		// Static size: 4
		std::uint32_t msgSize;

		UdpHeader()
		{
			magic = UdpHeader::MAGIC;
			payloadSize = 0;
			packetType = static_cast<std::uint8_t>(EUdpPacketType::Invalid);
			flags = 0;
			sourceConnID = 512;
			destConnID = 0;
			seqThis = 0;
			seqAck = 0;
			packetsInMsg = 0;
			msgStartSeq = 0;
			msgSize = 0;
		}
	};

	struct ChallengeData
	{
		static const std::uint32_t CHALLENGE_MASK = 0xA426DF2B;
		// Static size: 4
		std::uint32_t challengeValue;
		// Static size: 4
		std::uint32_t serverLoad;

		ChallengeData()
		{
			challengeValue = 0;
			serverLoad = 0;
		}
	};

	struct ConnectData
	{
		static const std::uint32_t CHALLENGE_MASK = ChallengeData::CHALLENGE_MASK;
		// Static size: 4
		std::uint32_t challengeValue;

		ConnectData()
		{
			challengeValue = 0;
		}
	};

	struct Accept
	{

		Accept()
		{
		}
	};

	struct Datagram
	{

		Datagram()
		{
		}
	};

	struct Disconnect
	{

		Disconnect()
		{
		}
	};

	struct MsgHdr
	{
		// Static size: 4
		std::uint32_t msg;
		// Static size: 8
		std::uint64_t targetJobID;
		// Static size: 8
		std::uint64_t sourceJobID;

		MsgHdr()
		{
			msg = static_cast<std::uint32_t>(EMsg::Invalid);
			targetJobID = UINT64_MAX;
			sourceJobID = UINT64_MAX;
		}
	};

	struct ExtendedClientMsgHdr
	{
		// Static size: 4
		std::uint32_t msg;
		// Static size: 1
		std::uint8_t headerSize;
		// Static size: 2
		std::uint16_t headerVersion;
		// Static size: 8
		std::uint64_t targetJobID;
		// Static size: 8
		std::uint64_t sourceJobID;
		// Static size: 1
		std::uint8_t headerCanary;
		// Static size: 8
		std::uint64_t steamID;
		// Static size: 4
		std::int32_t sessionID;

		ExtendedClientMsgHdr()
		{
			msg = static_cast<std::uint32_t>(EMsg::Invalid);
			headerSize = 36;
			headerVersion = 2;
			targetJobID = UINT64_MAX;
			sourceJobID = UINT64_MAX;
			headerCanary = 239;
			steamID = 0;
			sessionID = 0;
		}
	};

	struct MsgHdrProtoBuf
	{
		// Static size: 4
		std::uint32_t msg;
		// Static size: 4
		std::int32_t headerLength;
		// Static size: 0
		unsigned char proto[0];

		MsgHdrProtoBuf()
		{
			msg = static_cast<std::uint32_t>(EMsg::Invalid);
			headerLength = 0;
		}
	};

	struct MsgGCHdrProtoBuf
	{
		// Static size: 4
		std::uint32_t msg;
		// Static size: 4
		std::int32_t headerLength;
		// Static size: 0
		unsigned char proto[0];

		MsgGCHdrProtoBuf()
		{
			msg = 0;
			headerLength = 0;
		}
	};

	struct MsgGCHdr
	{
		// Static size: 2
		std::uint16_t headerVersion;
		// Static size: 8
		std::uint64_t targetJobID;
		// Static size: 8
		std::uint64_t sourceJobID;

		MsgGCHdr()
		{
			headerVersion = 1;
			targetJobID = UINT64_MAX;
			sourceJobID = UINT64_MAX;
		}
	};

	struct MsgClientJustStrings
	{

		MsgClientJustStrings()
		{
		}
	};

	struct MsgClientGenericResponse
	{
		// Static size: 4
		std::uint32_t result;

		MsgClientGenericResponse()
		{
			result = 0;
		}
	};

	struct MsgChannelEncryptRequest
	{
		static const std::uint32_t PROTOCOL_VERSION = 1;
		// Static size: 4
		std::uint32_t protocolVersion;
		// Static size: 4
		std::uint32_t universe;

		MsgChannelEncryptRequest()
		{
			protocolVersion = MsgChannelEncryptRequest::PROTOCOL_VERSION;
			universe = static_cast<std::uint32_t>(EUniverse::Invalid);
		}
	};

	struct MsgChannelEncryptResponse
	{
		// Static size: 4
		std::uint32_t protocolVersion;
		// Static size: 4
		std::uint32_t keySize;

		MsgChannelEncryptResponse()
		{
			protocolVersion = MsgChannelEncryptRequest::PROTOCOL_VERSION;
			keySize = 128;
		}
	};

	struct MsgChannelEncryptResult
	{
		// Static size: 4
		std::uint32_t result;

		MsgChannelEncryptResult()
		{
			result = static_cast<std::uint32_t>(EResult::Invalid);
		}
	};

	struct MsgClientNewLoginKey
	{
		// Static size: 4
		std::uint32_t uniqueID;
		// Static size: 20
		unsigned char loginKey[20];

		MsgClientNewLoginKey()
		{
			uniqueID = 0;
		}
	};

	struct MsgClientNewLoginKeyAccepted
	{
		// Static size: 4
		std::uint32_t uniqueID;

		MsgClientNewLoginKeyAccepted()
		{
			uniqueID = 0;
		}
	};

	struct MsgClientLogon
	{
		static const std::uint32_t ObfuscationMask = 0xBAADF00D;
		static const std::uint32_t CurrentProtocol = 65575;
		static const std::uint32_t ProtocolVerMajorMask = 0xFFFF0000;
		static const std::uint32_t ProtocolVerMinorMask = 0xFFFF;
		static const std::uint16_t ProtocolVerMinorMinGameServers = 4;
		static const std::uint16_t ProtocolVerMinorMinForSupportingEMsgMulti = 12;
		static const std::uint16_t ProtocolVerMinorMinForSupportingEMsgClientEncryptPct = 14;
		static const std::uint16_t ProtocolVerMinorMinForExtendedMsgHdr = 17;
		static const std::uint16_t ProtocolVerMinorMinForCellId = 18;
		static const std::uint16_t ProtocolVerMinorMinForSessionIDLast = 19;
		static const std::uint16_t ProtocolVerMinorMinForServerAvailablityMsgs = 24;
		static const std::uint16_t ProtocolVerMinorMinClients = 25;
		static const std::uint16_t ProtocolVerMinorMinForOSType = 26;
		static const std::uint16_t ProtocolVerMinorMinForCegApplyPESig = 27;
		static const std::uint16_t ProtocolVerMinorMinForMarketingMessages2 = 27;
		static const std::uint16_t ProtocolVerMinorMinForAnyProtoBufMessages = 28;
		static const std::uint16_t ProtocolVerMinorMinForProtoBufLoggedOffMessage = 28;
		static const std::uint16_t ProtocolVerMinorMinForProtoBufMultiMessages = 28;
		static const std::uint16_t ProtocolVerMinorMinForSendingProtocolToUFS = 30;
		static const std::uint16_t ProtocolVerMinorMinForMachineAuth = 33;

		MsgClientLogon()
		{
		}
	};

	struct MsgClientVACBanStatus
	{
		// Static size: 4
		std::uint32_t numBans;

		MsgClientVACBanStatus()
		{
			numBans = 0;
		}
	};

	struct MsgClientAppUsageEvent
	{
		// Static size: 4
		std::uint32_t appUsageEvent;
		// Static size: 8
		std::uint64_t gameID;
		// Static size: 2
		std::uint16_t offline;

		MsgClientAppUsageEvent()
		{
			appUsageEvent = 0;
			gameID = 0;
			offline = 0;
		}
	};

	struct MsgClientEmailAddrInfo
	{
		// Static size: 4
		std::uint32_t passwordStrength;
		// Static size: 4
		std::uint32_t flagsAccountSecurityPolicy;
		// Static size: 1
		std::uint8_t validated;

		MsgClientEmailAddrInfo()
		{
			passwordStrength = 0;
			flagsAccountSecurityPolicy = 0;
			validated = 0;
		}
	};

	struct MsgClientUpdateGuestPassesList
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 4
		std::int32_t countGuestPassesToGive;
		// Static size: 4
		std::int32_t countGuestPassesToRedeem;

		MsgClientUpdateGuestPassesList()
		{
			result = 0;
			countGuestPassesToGive = 0;
			countGuestPassesToRedeem = 0;
		}
	};

	struct MsgClientRequestedClientStats
	{
		// Static size: 4
		std::int32_t countStats;

		MsgClientRequestedClientStats()
		{
			countStats = 0;
		}
	};

	struct MsgClientP2PIntroducerMessage
	{
		// Static size: 8
		std::uint64_t steamID;
		// Static size: 4
		std::uint32_t routingType;
		// Static size: 1450
		unsigned char data[1450];
		// Static size: 4
		std::uint32_t dataLen;

		MsgClientP2PIntroducerMessage()
		{
			steamID = 0;
			routingType = 0;
			dataLen = 0;
		}
	};

	struct MsgClientOGSBeginSession
	{
		// Static size: 1
		std::uint8_t accountType;
		// Static size: 8
		std::uint64_t accountId;
		// Static size: 4
		std::uint32_t appId;
		// Static size: 4
		std::uint32_t timeStarted;

		MsgClientOGSBeginSession()
		{
			accountType = 0;
			accountId = 0;
			appId = 0;
			timeStarted = 0;
		}
	};

	struct MsgClientOGSBeginSessionResponse
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 1
		std::uint8_t collectingAny;
		// Static size: 1
		std::uint8_t collectingDetails;
		// Static size: 8
		std::uint64_t sessionId;

		MsgClientOGSBeginSessionResponse()
		{
			result = 0;
			collectingAny = 0;
			collectingDetails = 0;
			sessionId = 0;
		}
	};

	struct MsgClientOGSEndSession
	{
		// Static size: 8
		std::uint64_t sessionId;
		// Static size: 4
		std::uint32_t timeEnded;
		// Static size: 4
		std::int32_t reasonCode;
		// Static size: 4
		std::int32_t countAttributes;

		MsgClientOGSEndSession()
		{
			sessionId = 0;
			timeEnded = 0;
			reasonCode = 0;
			countAttributes = 0;
		}
	};

	struct MsgClientOGSEndSessionResponse
	{
		// Static size: 4
		std::uint32_t result;

		MsgClientOGSEndSessionResponse()
		{
			result = 0;
		}
	};

	struct MsgClientOGSWriteRow
	{
		// Static size: 8
		std::uint64_t sessionId;
		// Static size: 4
		std::int32_t countAttributes;

		MsgClientOGSWriteRow()
		{
			sessionId = 0;
			countAttributes = 0;
		}
	};

	struct MsgClientGetFriendsWhoPlayGame
	{
		// Static size: 8
		std::uint64_t gameId;

		MsgClientGetFriendsWhoPlayGame()
		{
			gameId = 0;
		}
	};

	struct MsgClientGetFriendsWhoPlayGameResponse
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 8
		std::uint64_t gameId;
		// Static size: 4
		std::uint32_t countFriends;

		MsgClientGetFriendsWhoPlayGameResponse()
		{
			result = 0;
			gameId = 0;
			countFriends = 0;
		}
	};

	struct MsgGSPerformHardwareSurvey
	{
		// Static size: 4
		std::uint32_t flags;

		MsgGSPerformHardwareSurvey()
		{
			flags = 0;
		}
	};

	struct MsgGSGetPlayStatsResponse
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 4
		std::int32_t rank;
		// Static size: 4
		std::uint32_t lifetimeConnects;
		// Static size: 4
		std::uint32_t lifetimeMinutesPlayed;

		MsgGSGetPlayStatsResponse()
		{
			result = 0;
			rank = 0;
			lifetimeConnects = 0;
			lifetimeMinutesPlayed = 0;
		}
	};

	struct MsgGSGetReputationResponse
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 4
		std::uint32_t reputationScore;
		// Static size: 1
		std::uint8_t banned;
		// Static size: 4
		std::uint32_t bannedIp;
		// Static size: 2
		std::uint16_t bannedPort;
		// Static size: 8
		std::uint64_t bannedGameId;
		// Static size: 4
		std::uint32_t timeBanExpires;

		MsgGSGetReputationResponse()
		{
			result = 0;
			reputationScore = 0;
			banned = 0;
			bannedIp = 0;
			bannedPort = 0;
			bannedGameId = 0;
			timeBanExpires = 0;
		}
	};

	struct MsgGSDeny
	{
		// Static size: 8
		std::uint64_t steamId;
		// Static size: 4
		std::uint32_t denyReason;

		MsgGSDeny()
		{
			steamId = 0;
			denyReason = 0;
		}
	};

	struct MsgGSApprove
	{
		// Static size: 8
		std::uint64_t steamId;

		MsgGSApprove()
		{
			steamId = 0;
		}
	};

	struct MsgGSKick
	{
		// Static size: 8
		std::uint64_t steamId;
		// Static size: 4
		std::uint32_t denyReason;
		// Static size: 4
		std::int32_t waitTilMapChange;

		MsgGSKick()
		{
			steamId = 0;
			denyReason = 0;
			waitTilMapChange = 0;
		}
	};

	struct MsgGSGetUserGroupStatus
	{
		// Static size: 8
		std::uint64_t steamIdUser;
		// Static size: 8
		std::uint64_t steamIdGroup;

		MsgGSGetUserGroupStatus()
		{
			steamIdUser = 0;
			steamIdGroup = 0;
		}
	};

	struct MsgGSGetUserGroupStatusResponse
	{
		// Static size: 8
		std::uint64_t steamIdUser;
		// Static size: 8
		std::uint64_t steamIdGroup;
		// Static size: 4
		std::uint32_t clanRelationship;
		// Static size: 4
		std::uint32_t clanRank;

		MsgGSGetUserGroupStatusResponse()
		{
			steamIdUser = 0;
			steamIdGroup = 0;
			clanRelationship = 0;
			clanRank = 0;
		}
	};

	struct MsgClientJoinChat
	{
		// Static size: 8
		std::uint64_t steamIdChat;
		// Static size: 1
		std::uint8_t isVoiceSpeaker;

		MsgClientJoinChat()
		{
			steamIdChat = 0;
			isVoiceSpeaker = 0;
		}
	};

	struct MsgClientChatEnter
	{
		// Static size: 8
		std::uint64_t steamIdChat;
		// Static size: 8
		std::uint64_t steamIdFriend;
		// Static size: 4
		std::uint32_t chatRoomType;
		// Static size: 8
		std::uint64_t steamIdOwner;
		// Static size: 8
		std::uint64_t steamIdClan;
		// Static size: 1
		std::uint8_t chatFlags;
		// Static size: 4
		std::uint32_t enterResponse;

		MsgClientChatEnter()
		{
			steamIdChat = 0;
			steamIdFriend = 0;
			chatRoomType = 0;
			steamIdOwner = 0;
			steamIdClan = 0;
			chatFlags = 0;
			enterResponse = 0;
		}
	};

	struct MsgClientChatMsg
	{
		// Static size: 8
		std::uint64_t steamIdChatter;
		// Static size: 8
		std::uint64_t steamIdChatRoom;
		// Static size: 4
		std::uint32_t chatMsgType;

		MsgClientChatMsg()
		{
			steamIdChatter = 0;
			steamIdChatRoom = 0;
			chatMsgType = 0;
		}
	};

	struct MsgClientChatMemberInfo
	{
		// Static size: 8
		std::uint64_t steamIdChat;
		// Static size: 4
		std::uint32_t type;

		MsgClientChatMemberInfo()
		{
			steamIdChat = 0;
			type = 0;
		}
	};

	struct MsgClientChatAction
	{
		// Static size: 8
		std::uint64_t steamIdChat;
		// Static size: 8
		std::uint64_t steamIdUserToActOn;
		// Static size: 4
		std::uint32_t chatAction;

		MsgClientChatAction()
		{
			steamIdChat = 0;
			steamIdUserToActOn = 0;
			chatAction = 0;
		}
	};

	struct MsgClientChatActionResult
	{
		// Static size: 8
		std::uint64_t steamIdChat;
		// Static size: 8
		std::uint64_t steamIdUserActedOn;
		// Static size: 4
		std::uint32_t chatAction;
		// Static size: 4
		std::uint32_t actionResult;

		MsgClientChatActionResult()
		{
			steamIdChat = 0;
			steamIdUserActedOn = 0;
			chatAction = 0;
			actionResult = 0;
		}
	};

	struct MsgClientGetNumberOfCurrentPlayers
	{
		// Static size: 8
		std::uint64_t gameID;

		MsgClientGetNumberOfCurrentPlayers()
		{
			gameID = 0;
		}
	};

	struct MsgClientGetNumberOfCurrentPlayersResponse
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 4
		std::uint32_t numPlayers;

		MsgClientGetNumberOfCurrentPlayersResponse()
		{
			result = 0;
			numPlayers = 0;
		}
	};

	struct MsgClientSetIgnoreFriend
	{
		// Static size: 8
		std::uint64_t mySteamId;
		// Static size: 8
		std::uint64_t steamIdFriend;
		// Static size: 1
		std::uint8_t ignore;

		MsgClientSetIgnoreFriend()
		{
			mySteamId = 0;
			steamIdFriend = 0;
			ignore = 0;
		}
	};

	struct MsgClientSetIgnoreFriendResponse
	{
		// Static size: 8
		std::uint64_t unknown;
		// Static size: 4
		std::uint32_t result;

		MsgClientSetIgnoreFriendResponse()
		{
			unknown = 0;
			result = 0;
		}
	};

	struct MsgClientLoggedOff
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 4
		std::int32_t secMinReconnectHint;
		// Static size: 4
		std::int32_t secMaxReconnectHint;

		MsgClientLoggedOff()
		{
			result = 0;
			secMinReconnectHint = 0;
			secMaxReconnectHint = 0;
		}
	};

	struct MsgClientLogOnResponse
	{
		// Static size: 4
		std::uint32_t result;
		// Static size: 4
		std::int32_t outOfGameHeartbeatRateSec;
		// Static size: 4
		std::int32_t inGameHeartbeatRateSec;
		// Static size: 8
		std::uint64_t clientSuppliedSteamId;
		// Static size: 4
		std::uint32_t ipPublic;
		// Static size: 4
		std::uint32_t serverRealTime;

		MsgClientLogOnResponse()
		{
			result = 0;
			outOfGameHeartbeatRateSec = 0;
			inGameHeartbeatRateSec = 0;
			clientSuppliedSteamId = 0;
			ipPublic = 0;
			serverRealTime = 0;
		}
	};

	struct MsgClientSendGuestPass
	{
		// Static size: 8
		std::uint64_t giftId;
		// Static size: 1
		std::uint8_t giftType;
		// Static size: 4
		std::uint32_t accountId;

		MsgClientSendGuestPass()
		{
			giftId = 0;
			giftType = 0;
			accountId = 0;
		}
	};

	struct MsgClientSendGuestPassResponse
	{
		// Static size: 4
		std::uint32_t result;

		MsgClientSendGuestPassResponse()
		{
			result = 0;
		}
	};

	struct MsgClientServerUnavailable
	{
		// Static size: 8
		std::uint64_t jobidSent;
		// Static size: 4
		std::uint32_t eMsgSent;
		// Static size: 4
		std::uint32_t eServerTypeUnavailable;

		MsgClientServerUnavailable()
		{
			jobidSent = 0;
			eMsgSent = 0;
			eServerTypeUnavailable = 0;
		}
	};

}
#pragma pack(pop)
