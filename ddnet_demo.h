/**
 * @file ddnet_demo.h
 * @brief A single-header C99 library for reading, writing, and creating DDNet 0.6 demo files.
 * @version 1.0
 *
 * To use this library, do this in one C file:
 * #define DDNET_DEMO_IMPLEMENTATION
 * #include "ddnet_demo.h"
 *
 * You can then include "ddnet_demo.h" in other parts of the project.
 */

#ifndef DDNET_DEMO_H
#define DDNET_DEMO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *
 * CONSTANTS AND BASIC TYPES
 *
 ******************************************************************************/

#define DD_SERVER_TICK_SPEED 50
#define DD_MAX_TIMELINE_MARKERS 64
#define DD_MAX_SNAPSHOT_ITEMS 1024
#define DD_MAX_SNAPSHOT_SIZE (DD_MAX_SNAPSHOT_ITEMS * 256) // Increased size for safety
#define DD_MAX_NETOBJSIZES 64
#define DD_MAX_PAYLOAD (DD_MAX_SNAPSHOT_SIZE + 4096)
#define DD_MAX_TYPE 0x7fff

/* Demo chunk types that can be returned by the reader */
enum {
  DD_CHUNK_INVALID = 0,
  DD_CHUNK_SNAP,
  DD_CHUNK_SNAP_DELTA,
  DD_CHUNK_MSG,
  DD_CHUNK_TICK_MARKER
};

/* Raw demo file header structure. Multi-byte fields are big-endian. */
typedef struct {
  uint8_t marker[7];
  uint8_t version;
  char net_version[64];
  char map_name[64];
  uint8_t map_size[4];
  uint8_t map_crc[4];
  char type[8];
  uint8_t length[4];
  char timestamp[20];
} dd_demo_header;

/* Raw timeline markers structure from the demo file. */
typedef struct {
  uint8_t num_markers[4];
  uint8_t markers[DD_MAX_TIMELINE_MARKERS][4];
} dd_timeline_markers;

/* Processed demo info, easily accessible from the reader. */
typedef struct {
  dd_demo_header header;
  dd_timeline_markers timeline_markers_raw;
  uint32_t map_size;
  uint32_t map_crc;
  int length;
  int num_markers;
  int markers[DD_MAX_TIMELINE_MARKERS];
  uint8_t map_sha256[32];
  bool has_sha256;
} dd_demo_info;

/* Represents a single data chunk read from the demo. */
typedef struct {
  int type;
  int tick;
  bool is_keyframe;
  int size;
  const uint8_t *data;
} dd_demo_chunk;

/* Snapshot item structure */
typedef struct {
  int type_and_id;
} dd_snap_item;

static inline int dd_snap_item_type(const dd_snap_item *item) { return item->type_and_id >> 16; }
static inline int dd_snap_item_id(const dd_snap_item *item) { return item->type_and_id & 0xffff; }
static inline int dd_snap_item_key(const dd_snap_item *item) { return item->type_and_id; }
static inline int *dd_snap_item_data(const dd_snap_item *item) { return (int *)(item + 1); }

/* Main snapshot structure */
typedef struct {
  int data_size;
  int num_items;
  /* Offsets and data follow this struct in memory */
} dd_snapshot;

static inline int *dd_snap_offsets(const dd_snapshot *snap) { return (int *)(snap + 1); }
static inline char *dd_snap_data_start(const dd_snapshot *snap) { return (char *)(dd_snap_offsets(snap) + snap->num_items); }
static inline const dd_snap_item *dd_snap_get_item(const dd_snapshot *snap, int index) {
  if (index < 0 || index >= snap->num_items) return NULL;
  return (const dd_snap_item *)(dd_snap_data_start(snap) + dd_snap_offsets(snap)[index]);
}
int dd_snap_get_item_size(const dd_snapshot *snap, int index);
const dd_snap_item *dd_snap_find_item(const dd_snapshot *snap, int type, int id);

/******************************************************************************
 *
 * 0.6 & 0.7 PROTOCOL DEFINITIONS (although we don't support 0.7 demos yet)
 *
 ******************************************************************************/

#define DD_GAMEINFO_CURVERSION 10
#define OFFSET_UUID 256

/* Vanilla Object and Event types */
enum {
  DD_NETOBJTYPE_EX,
  DD_NETOBJTYPE_PLAYERINPUT,
  DD_NETOBJTYPE_PROJECTILE,
  DD_NETOBJTYPE_LASER,
  DD_NETOBJTYPE_PICKUP,
  DD_NETOBJTYPE_FLAG,
  DD_NETOBJTYPE_GAMEINFO,
  DD_NETOBJTYPE_GAMEDATA,
  DD_NETOBJTYPE_CHARACTERCORE,
  DD_NETOBJTYPE_CHARACTER,
  DD_NETOBJTYPE_PLAYERINFO,
  DD_NETOBJTYPE_CLIENTINFO,
  DD_NETOBJTYPE_SPECTATORINFO,
  DD_NETEVENTTYPE_COMMON,
  DD_NETEVENTTYPE_EXPLOSION,
  DD_NETEVENTTYPE_SPAWN,
  DD_NETEVENTTYPE_HAMMERHIT,
  DD_NETEVENTTYPE_DEATH,
  DD_NETEVENTTYPE_SOUNDGLOBAL,
  DD_NETEVENTTYPE_SOUNDWORLD,
  DD_NETEVENTTYPE_DAMAGEIND,
  DD_NUM_VANILLA_NETOBJTYPES
};

/* Extended Object and Event types */
enum {
  DD_NETOBJTYPE_MYOWNOBJECT = OFFSET_UUID,
  DD_NETOBJTYPE_DDNETCHARACTER,
  DD_NETOBJTYPE_DDNETPLAYER,
  DD_NETOBJTYPE_GAMEINFOEX,
  DD_NETOBJTYPE_DDRACEPROJECTILE,
  DD_NETOBJTYPE_DDNETLASER,
  DD_NETOBJTYPE_DDNETPROJECTILE,
  DD_NETOBJTYPE_DDNETPICKUP,
  DD_NETOBJTYPE_DDNETSPECTATORINFO,
  DD_NETEVENTTYPE_BIRTHDAY,
  DD_NETEVENTTYPE_FINISH,
  DD_NETOBJTYPE_MYOWNEVENT,
  DD_NETOBJTYPE_SPECCHAR,
  DD_NETOBJTYPE_SWITCHSTATE,
  DD_NETOBJTYPE_ENTITYEX,
  DD_NETEVENTTYPE_MAPSOUNDWORLD,
  DD_OFFSET_NETMSGTYPE_UUID
};

/* Vanilla Message types */
enum {
  DD_NETMSGTYPE_EX,
  DD_NETMSGTYPE_SV_MOTD,
  DD_NETMSGTYPE_SV_BROADCAST,
  DD_NETMSGTYPE_SV_CHAT,
  DD_NETMSGTYPE_SV_KILLMSG,
  DD_NETMSGTYPE_SV_SOUNDGLOBAL,
  DD_NETMSGTYPE_SV_TUNEPARAMS,
  DD_NETMSGTYPE_UNUSED,
  DD_NETMSGTYPE_SV_READYTOENTER,
  DD_NETMSGTYPE_SV_WEAPONPICKUP,
  DD_NETMSGTYPE_SV_EMOTICON,
  DD_NETMSGTYPE_SV_VOTECLEAROPTIONS,
  DD_NETMSGTYPE_SV_VOTEOPTIONLISTADD,
  DD_NETMSGTYPE_SV_VOTEOPTIONADD,
  DD_NETMSGTYPE_SV_VOTEOPTIONREMOVE,
  DD_NETMSGTYPE_SV_VOTESET,
  DD_NETMSGTYPE_SV_VOTESTATUS,
  DD_NETMSGTYPE_CL_SAY,
  DD_NETMSGTYPE_CL_SETTEAM,
  DD_NETMSGTYPE_CL_SETSPECTATORMODE,
  DD_NETMSGTYPE_CL_STARTINFO,
  DD_NETMSGTYPE_CL_CHANGEINFO,
  DD_NETMSGTYPE_CL_KILL,
  DD_NETMSGTYPE_CL_EMOTICON,
  DD_NETMSGTYPE_CL_VOTE,
  DD_NETMSGTYPE_CL_CALLVOTE,
  DD_NETMSGTYPE_CL_ISDDNETLEGACY,
  DD_NETMSGTYPE_SV_DDRACETIMELEGACY,
  DD_NETMSGTYPE_SV_RECORDLEGACY,
  DD_NETMSGTYPE_UNUSED2,
  DD_NETMSGTYPE_SV_TEAMSSTATELEGACY,
  DD_NETMSGTYPE_CL_SHOWOTHERSLEGACY,
  DD_NUM_VANILLA_NETMSGTYPES
};

/* Extended Message types */
enum {
  DD_NETMSG_WHATIS = DD_OFFSET_NETMSGTYPE_UUID,
  DD_NETMSG_ITIS,
  DD_NETMSG_IDONTKNOW,
  DD_NETMSG_RCONTYPE,
  DD_NETMSG_MAP_DETAILS,
  DD_NETMSG_CAPABILITIES,
  DD_NETMSG_CLIENTVER,
  DD_NETMSG_PINGEX,
  DD_NETMSG_PONGEX,
  DD_NETMSG_CHECKSUM_REQUEST,
  DD_NETMSG_CHECKSUM_RESPONSE,
  DD_NETMSG_CHECKSUM_ERROR,
  DD_NETMSG_REDIRECT,
  DD_NETMSG_RCON_CMD_GROUP_START,
  DD_NETMSG_RCON_CMD_GROUP_END,
  DD_NETMSG_MAP_RELOAD,
  DD_NETMSG_RECONNECT,
  DD_NETMSG_SV_MAPLIST_ADD,
  DD_NETMSG_SV_MAPLIST_GROUP_START,
  DD_NETMSG_SV_MAPLIST_GROUP_END,
  DD_NETMSGTYPE_SV_MYOWNMESSAGE,
  DD_NETMSGTYPE_CL_SHOWDISTANCE,
  DD_NETMSGTYPE_CL_SHOWOTHERS,
  DD_NETMSGTYPE_CL_CAMERAINFO,
  DD_NETMSGTYPE_SV_TEAMSSTATE,
  DD_NETMSGTYPE_SV_DDRACETIME,
  DD_NETMSGTYPE_SV_RECORD,
  DD_NETMSGTYPE_SV_KILLMSGTEAM,
  DD_NETMSGTYPE_SV_YOURVOTE,
  DD_NETMSGTYPE_SV_RACEFINISH,
  DD_NETMSGTYPE_SV_COMMANDINFO,
  DD_NETMSGTYPE_SV_COMMANDINFOREMOVE,
  DD_NETMSGTYPE_SV_VOTEOPTIONGROUPSTART,
  DD_NETMSGTYPE_SV_VOTEOPTIONGROUPEND,
  DD_NETMSGTYPE_SV_COMMANDINFOGROUPSTART,
  DD_NETMSGTYPE_SV_COMMANDINFOGROUPEND,
  DD_NETMSGTYPE_SV_CHANGEINFOCOOLDOWN,
  DD_NETMSGTYPE_SV_MAPSOUNDGLOBAL,
  DD_NETMSGTYPE_SV_PREINPUT,
};

/* Laser subtypes (used in dd_netobj_ddnet_laser) */
enum {
  DD_LASERTYPE_RIFLE,
  DD_LASERTYPE_SHOTGUN,
  DD_LASERTYPE_DOOR,
  DD_LASERTYPE_FREEZE,
  DD_LASERTYPE_DRAGGER,
  DD_LASERTYPE_GUN,
  DD_LASERTYPE_PLASMA,
};

/* Dragger subtypes (used in dd_netobj_ddnet_laser) */
enum {
  DD_LASERDRAGGERTYPE_WEAK,
  DD_LASERDRAGGERTYPE_WEAK_NW,
  DD_LASERDRAGGERTYPE_NORMAL,
  DD_LASERDRAGGERTYPE_NORMAL_NW,
  DD_LASERDRAGGERTYPE_STRONG,
  DD_LASERDRAGGERTYPE_STRONG_NW,
};

/* Gun subtypes (used in dd_netobj_ddnet_laser) */
enum {
  DD_LASERGUNTYPE_UNFREEZE,
  DD_LASERGUNTYPE_EXPLOSIVE,
  DD_LASERGUNTYPE_FREEZE,
  DD_LASERGUNTYPE_EXPFREEZE,
};

/* EntityEx entity classes */
enum {
  DD_ENTITYCLASS_PROJECTILE,
  DD_ENTITYCLASS_DOOR,
  DD_ENTITYCLASS_DRAGGER_WEAK,
  DD_ENTITYCLASS_DRAGGER_NORMAL,
  DD_ENTITYCLASS_DRAGGER_STRONG,
  DD_ENTITYCLASS_GUN_NORMAL,
  DD_ENTITYCLASS_GUN_EXPLOSIVE,
  DD_ENTITYCLASS_GUN_FREEZE,
  DD_ENTITYCLASS_GUN_UNFREEZE,
  DD_ENTITYCLASS_LIGHT,
  DD_ENTITYCLASS_PICKUP,
};

/* Game sounds */
enum {
  DD_SOUND_GUN_FIRE = 0,
  DD_SOUND_SHOTGUN_FIRE,
  DD_SOUND_GRENADE_FIRE,
  DD_SOUND_HAMMER_FIRE,
  DD_SOUND_HAMMER_HIT,
  DD_SOUND_NINJA_FIRE,
  DD_SOUND_GRENADE_EXPLODE,
  DD_SOUND_NINJA_HIT,
  DD_SOUND_LASER_FIRE,
  DD_SOUND_LASER_BOUNCE,
  DD_SOUND_WEAPON_SWITCH,
  DD_SOUND_PLAYER_PAIN_SHORT,
  DD_SOUND_PLAYER_PAIN_LONG,
  DD_SOUND_BODY_LAND,
  DD_SOUND_PLAYER_AIRJUMP,
  DD_SOUND_PLAYER_JUMP,
  DD_SOUND_PLAYER_DIE,
  DD_SOUND_PLAYER_SPAWN,
  DD_SOUND_PLAYER_SKID,
  DD_SOUND_TEE_CRY,
  DD_SOUND_HOOK_LOOP,
  DD_SOUND_HOOK_ATTACH_GROUND,
  DD_SOUND_HOOK_ATTACH_PLAYER,
  DD_SOUND_HOOK_NOATTACH,
  DD_SOUND_PICKUP_HEALTH,
  DD_SOUND_PICKUP_ARMOR,
  DD_SOUND_PICKUP_GRENADE,
  DD_SOUND_PICKUP_SHOTGUN,
  DD_SOUND_PICKUP_NINJA,
  DD_SOUND_WEAPON_SPAWN,
  DD_SOUND_WEAPON_NOAMMO,
  DD_SOUND_HIT,
  DD_SOUND_CHAT_SERVER,
  DD_SOUND_CHAT_CLIENT,
  DD_SOUND_CHAT_HIGHLIGHT,
  DD_SOUND_CTF_DROP,
  DD_SOUND_CTF_RETURN,
  DD_SOUND_CTF_GRAB_PL,
  DD_SOUND_CTF_GRAB_EN,
  DD_SOUND_CTF_CAPTURE,
  DD_SOUND_MENU,
  DD_NUM_SOUNDS
};

/* Weapons */
enum {
  DD_WEAPON_HAMMER = 0,
  DD_WEAPON_GUN,
  DD_WEAPON_SHOTGUN,
  DD_WEAPON_GRENADE,
  DD_WEAPON_LASER,
  DD_WEAPON_NINJA,
  DD_NUM_WEAPONS,
  DD_WEAPON_GAME = -3,
  DD_WEAPON_SELF = -2,
  DD_WEAPON_WORLD = -1,
};

/* Powerups */
enum {
  DD_POWERUP_HEALTH,
  DD_POWERUP_ARMOR,
  DD_POWERUP_WEAPON,
  DD_POWERUP_NINJA,
  DD_POWERUP_ARMOR_SHOTGUN,
  DD_POWERUP_ARMOR_GRENADE,
  DD_POWERUP_ARMOR_NINJA,
  DD_POWERUP_ARMOR_LASER,
  DD_NUM_POWERUPS
};

/* Emotes */
enum {
  DD_EMOTE_NORMAL,
  DD_EMOTE_PAIN,
  DD_EMOTE_HAPPY,
  DD_EMOTE_SURPRISE,
  DD_EMOTE_ANGRY,
  DD_EMOTE_BLINK,
  DD_NUM_EMOTES
};

/******************************************************************************
 *
 * BITMASK FLAGS
 *
 ******************************************************************************/

enum {
  DD_PLAYERFLAG_PLAYING = 1 << 0,
  DD_PLAYERFLAG_IN_MENU = 1 << 1,
  DD_PLAYERFLAG_CHATTING = 1 << 2,
  DD_PLAYERFLAG_SCOREBOARD = 1 << 3,
  DD_PLAYERFLAG_AIM = 1 << 4,
  DD_PLAYERFLAG_SPEC_CAM = 1 << 5,
};
enum {
  DD_GAMEFLAG_TEAMS = 1 << 0,
  DD_GAMEFLAG_FLAGS = 1 << 1,
};
enum {
  DD_GAMESTATEFLAG_GAMEOVER = 1 << 0,
  DD_GAMESTATEFLAG_SUDDENDEATH = 1 << 1,
  DD_GAMESTATEFLAG_PAUSED = 1 << 2,
  DD_GAMESTATEFLAG_RACETIME = 1 << 3,
};
enum {
  DD_CHARACTERFLAG_SOLO = 1 << 0,
  DD_CHARACTERFLAG_JETPACK = 1 << 1,
  DD_CHARACTERFLAG_COLLISION_DISABLED = 1 << 2,
  DD_CHARACTERFLAG_ENDLESS_HOOK = 1 << 3,
  DD_CHARACTERFLAG_ENDLESS_JUMP = 1 << 4,
  DD_CHARACTERFLAG_SUPER = 1 << 5,
  DD_CHARACTERFLAG_HAMMER_HIT_DISABLED = 1 << 6,
  DD_CHARACTERFLAG_SHOTGUN_HIT_DISABLED = 1 << 7,
  DD_CHARACTERFLAG_GRENADE_HIT_DISABLED = 1 << 8,
  DD_CHARACTERFLAG_LASER_HIT_DISABLED = 1 << 9,
  DD_CHARACTERFLAG_HOOK_HIT_DISABLED = 1 << 10,
  DD_CHARACTERFLAG_TELEGUN_GUN = 1 << 11,
  DD_CHARACTERFLAG_TELEGUN_GRENADE = 1 << 12,
  DD_CHARACTERFLAG_TELEGUN_LASER = 1 << 13,
  DD_CHARACTERFLAG_WEAPON_HAMMER = 1 << 14,
  DD_CHARACTERFLAG_WEAPON_GUN = 1 << 15,
  DD_CHARACTERFLAG_WEAPON_SHOTGUN = 1 << 16,
  DD_CHARACTERFLAG_WEAPON_GRENADE = 1 << 17,
  DD_CHARACTERFLAG_WEAPON_LASER = 1 << 18,
  DD_CHARACTERFLAG_WEAPON_NINJA = 1 << 19,
  DD_CHARACTERFLAG_MOVEMENTS_DISABLED = 1 << 20,
  DD_CHARACTERFLAG_IN_FREEZE = 1 << 21,
  DD_CHARACTERFLAG_PRACTICE_MODE = 1 << 22,
  DD_CHARACTERFLAG_LOCK_MODE = 1 << 23,
  DD_CHARACTERFLAG_TEAM0_MODE = 1 << 24,
  DD_CHARACTERFLAG_INVINCIBLE = 1 << 25,
};
enum {
  DD_GAMEINFOFLAG_TIMESCORE = 1 << 0,
  DD_GAMEINFOFLAG_GAMETYPE_RACE = 1 << 1,
  DD_GAMEINFOFLAG_GAMETYPE_FASTCAP = 1 << 2,
  DD_GAMEINFOFLAG_GAMETYPE_FNG = 1 << 3,
  DD_GAMEINFOFLAG_GAMETYPE_DDRACE = 1 << 4,
  DD_GAMEINFOFLAG_GAMETYPE_DDNET = 1 << 5,
  DD_GAMEINFOFLAG_GAMETYPE_BLOCK_WORLDS = 1 << 6,
  DD_GAMEINFOFLAG_GAMETYPE_VANILLA = 1 << 7,
  DD_GAMEINFOFLAG_GAMETYPE_PLUS = 1 << 8,
  DD_GAMEINFOFLAG_FLAG_STARTS_RACE = 1 << 9,
  DD_GAMEINFOFLAG_RACE = 1 << 10,
  DD_GAMEINFOFLAG_UNLIMITED_AMMO = 1 << 11,
  DD_GAMEINFOFLAG_DDRACE_RECORD_MESSAGE = 1 << 12,
  DD_GAMEINFOFLAG_RACE_RECORD_MESSAGE = 1 << 13,
  DD_GAMEINFOFLAG_ALLOW_EYE_WHEEL = 1 << 14,
  DD_GAMEINFOFLAG_ALLOW_HOOK_COLL = 1 << 15,
  DD_GAMEINFOFLAG_ALLOW_ZOOM = 1 << 16,
  DD_GAMEINFOFLAG_BUG_DDRACE_GHOST = 1 << 17,
  DD_GAMEINFOFLAG_BUG_DDRACE_INPUT = 1 << 18,
  DD_GAMEINFOFLAG_BUG_FNG_LASER_RANGE = 1 << 19,
  DD_GAMEINFOFLAG_BUG_VANILLA_BOUNCE = 1 << 20,
  DD_GAMEINFOFLAG_PREDICT_FNG = 1 << 21,
  DD_GAMEINFOFLAG_PREDICT_DDRACE = 1 << 22,
  DD_GAMEINFOFLAG_PREDICT_DDRACE_TILES = 1 << 23,
  DD_GAMEINFOFLAG_PREDICT_VANILLA = 1 << 24,
  DD_GAMEINFOFLAG_ENTITIES_DDNET = 1 << 25,
  DD_GAMEINFOFLAG_ENTITIES_DDRACE = 1 << 26,
  DD_GAMEINFOFLAG_ENTITIES_RACE = 1 << 27,
  DD_GAMEINFOFLAG_ENTITIES_FNG = 1 << 28,
  DD_GAMEINFOFLAG_ENTITIES_VANILLA = 1 << 29,
  DD_GAMEINFOFLAG_DONT_MASK_ENTITIES = 1 << 30,
  DD_GAMEINFOFLAG_ENTITIES_BW = 1 << 31,
};
enum {
  DD_GAMEINFOFLAG2_ALLOW_X_SKINS = 1 << 0,
  DD_GAMEINFOFLAG2_GAMETYPE_CITY = 1 << 1,
  DD_GAMEINFOFLAG2_GAMETYPE_FDDRACE = 1 << 2,
  DD_GAMEINFOFLAG2_ENTITIES_FDDRACE = 1 << 3,
  DD_GAMEINFOFLAG2_HUD_HEALTH_ARMOR = 1 << 4,
  DD_GAMEINFOFLAG2_HUD_AMMO = 1 << 5,
  DD_GAMEINFOFLAG2_HUD_DDRACE = 1 << 6,
  DD_GAMEINFOFLAG2_NO_WEAK_HOOK = 1 << 7,
  DD_GAMEINFOFLAG2_NO_SKIN_CHANGE_FOR_FROZEN = 1 << 8,
  DD_GAMEINFOFLAG2_DDRACE_TEAM = 1 << 9,
};
enum {
  DD_EXPLAYERFLAG_AFK = 1 << 0,
  DD_EXPLAYERFLAG_PAUSED = 1 << 1,
  DD_EXPLAYERFLAG_SPEC = 1 << 2,
};
enum {
  DD_PROJECTILEFLAG_BOUNCE_HORIZONTAL = 1 << 0,
  DD_PROJECTILEFLAG_BOUNCE_VERTICAL = 1 << 1,
  DD_PROJECTILEFLAG_EXPLOSIVE = 1 << 2,
  DD_PROJECTILEFLAG_FREEZE = 1 << 3,
  DD_PROJECTILEFLAG_NORMALIZE_VEL = 1 << 4,
};
enum {
  DD_LASERFLAG_NO_PREDICT = 1 << 0,
};
enum {
  DD_PICKUPFLAG_XFLIP = 1 << 0,
  DD_PICKUPFLAG_YFLIP = 1 << 1,
  DD_PICKUPFLAG_ROTATE = 1 << 2,
  DD_PICKUPFLAG_NO_PREDICT = 1 << 3,
};

/******************************************************************************
 *
 * NETWORK OBJECT STRUCTURES
 *
 ******************************************************************************/

typedef struct {
  int m_Direction;
  int m_TargetX;
  int m_TargetY;
  int m_Jump;
  int m_Fire;
  int m_Hook;
  int m_PlayerFlags;
  int m_WantedWeapon;
  int m_NextWeapon;
  int m_PrevWeapon;
} dd_netobj_player_input;
typedef struct {
  int m_X;
  int m_Y;
  int m_VelX;
  int m_VelY;
  int m_Type;
  int m_StartTick;
} dd_netobj_projectile;
typedef struct {
  int m_X;
  int m_Y;
  int m_FromX;
  int m_FromY;
  int m_StartTick;
} dd_netobj_laser;
typedef struct {
  int m_X;
  int m_Y;
  int m_Type;
  int m_Subtype;
} dd_netobj_pickup;
typedef struct {
  int m_X;
  int m_Y;
  int m_Team;
} dd_netobj_flag;
typedef struct {
  int m_GameFlags;
  int m_GameStateFlags;
  int m_RoundStartTick;
  int m_WarmupTimer;
  int m_ScoreLimit;
  int m_TimeLimit;
  int m_RoundNum;
  int m_RoundCurrent;
} dd_netobj_game_info;
typedef struct {
  int m_TeamscoreRed;
  int m_TeamscoreBlue;
  int m_FlagCarrierRed;
  int m_FlagCarrierBlue;
} dd_netobj_game_data;
typedef struct {
  int m_Tick;
  int m_X;
  int m_Y;
  int m_VelX;
  int m_VelY;
  int m_Angle;
  int m_Direction;
  int m_Jumped;
  int m_HookedPlayer;
  int m_HookState;
  int m_HookTick;
  int m_HookX;
  int m_HookY;
  int m_HookDx;
  int m_HookDy;
} dd_netobj_character_core;
typedef struct {
  dd_netobj_character_core core;
  int m_PlayerFlags;
  int m_Health;
  int m_Armor;
  int m_AmmoCount;
  int m_Weapon;
  int m_Emote;
  int m_AttackTick;
} dd_netobj_character;
typedef struct {
  int m_Local;
  int m_ClientId;
  int m_Team;
  int m_Score;
  int m_Latency;
} dd_netobj_player_info;
typedef struct {
  int m_aName[4];
  int m_aClan[3];
  int m_Country;
  int m_aSkin[6];
  int m_UseCustomColor;
  int m_ColorBody;
  int m_ColorFeet;
} dd_netobj_client_info;
typedef struct {
  int m_SpectatorId;
  int m_X;
  int m_Y;
} dd_netobj_spectator_info;
typedef struct {
  int m_X;
  int m_Y;
} dd_netevent_common;
typedef struct {
  dd_netevent_common common;
} dd_netevent_explosion;
typedef struct {
  dd_netevent_common common;
} dd_netevent_spawn;
typedef struct {
  dd_netevent_common common;
} dd_netevent_hammer_hit;
typedef struct {
  dd_netevent_common common;
  int m_ClientId;
} dd_netevent_death;
typedef struct {
  dd_netevent_common common;
  int m_SoundId;
} dd_netevent_sound_global;
typedef struct {
  dd_netevent_common common;
  int m_SoundId;
} dd_netevent_sound_world;
typedef struct {
  dd_netevent_common common;
  int m_Angle;
} dd_netevent_damage_ind;
typedef struct {
  int m_Flags;
  int m_FreezeEnd;
  int m_Jumps;
  int m_TeleCheckpoint;
  int m_StrongWeakId;
  int m_JumpedTotal;
  int m_NinjaActivationTick;
  int m_FreezeStart;
  int m_TargetX;
  int m_TargetY;
  int m_TuneZoneOverride;
} dd_netobj_ddnet_character;
typedef struct {
  int m_Flags;
  int m_AuthLevel;
} dd_netobj_ddnet_player;
typedef struct {
  int m_Flags;
  int m_Version;
  int m_Flags2;
} dd_netobj_game_info_ex;
typedef struct {
  int m_X;
  int m_Y;
  int m_Angle;
  int m_Data;
  int m_Type;
  int m_StartTick;
} dd_netobj_ddrace_projectile;
typedef struct {
  int m_ToX;
  int m_ToY;
  int m_FromX;
  int m_FromY;
  int m_StartTick;
  int m_Owner;
  int m_Type;
  int m_SwitchNumber;
  int m_Subtype;
  int m_Flags;
} dd_netobj_ddnet_laser;
typedef struct {
  int m_X;
  int m_Y;
  int m_VelX;
  int m_VelY;
  int m_Type;
  int m_StartTick;
  int m_Owner;
  int m_SwitchNumber;
  int m_TuneZone;
  int m_Flags;
} dd_netobj_ddnet_projectile;
typedef struct {
  int m_X;
  int m_Y;
  int m_Type;
  int m_Subtype;
  int m_SwitchNumber;
  int m_Flags;
} dd_netobj_ddnet_pickup;
typedef struct {
  int m_HasCameraInfo;
  int m_Zoom;
  int m_Deadzone;
  int m_FollowFactor;
  int m_SpectatorCount;
} dd_netobj_ddnet_spectator_info;
typedef struct {
  dd_netevent_common common;
} dd_netevent_birthday;
typedef struct {
  dd_netevent_common common;
} dd_netevent_finish;
typedef struct {
  int m_X;
  int m_Y;
} dd_netobj_spec_char;
typedef struct {
  int m_HighestSwitchNumber;
  int m_aStatus[8];
  int m_aSwitchNumbers[4];
  int m_aEndTicks[4];
} dd_netobj_switch_state;
typedef struct {
  int m_SwitchNumber;
  int m_Layer;
  int m_EntityClass;
} dd_netobj_entity_ex;
typedef struct {
  dd_netevent_common common;
  int m_SoundId;
} dd_netevent_map_sound_world;

/******************************************************************************
 *
 * NETWORK MESSAGE STRUCTURES
 *
 ******************************************************************************/
typedef struct {
  const char *m_pMessage;
} dd_netmsg_sv_motd;
typedef struct {
  const char *m_pMessage;
} dd_netmsg_sv_broadcast;
typedef struct {
  int m_Team;
  int m_ClientId;
  const char *m_pMessage;
} dd_netmsg_sv_chat;
typedef struct {
  int m_Killer;
  int m_Victim;
  int m_Weapon;
  int m_ModeSpecial;
} dd_netmsg_sv_killmsg;
typedef struct {
  int m_SoundId;
} dd_netmsg_sv_sound_global;
typedef struct {
  int m_Weapon;
} dd_netmsg_sv_weaponpickup;
typedef struct {
  int m_ClientId;
  int m_Emoticon;
} dd_netmsg_sv_emoticon;
typedef struct {
  int m_NumOptions;
  const char *m_pDescription0;
  const char *m_pDescription1;
  const char *m_pDescription2;
  const char *m_pDescription3;
  const char *m_pDescription4;
  const char *m_pDescription5;
  const char *m_pDescription6;
  const char *m_pDescription7;
  const char *m_pDescription8;
  const char *m_pDescription9;
  const char *m_pDescription10;
  const char *m_pDescription11;
  const char *m_pDescription12;
  const char *m_pDescription13;
  const char *m_pDescription14;
} dd_netmsg_sv_vote_option_list_add;
typedef struct {
  const char *m_pDescription;
} dd_netmsg_sv_vote_option_add;
typedef struct {
  const char *m_pDescription;
} dd_netmsg_sv_vote_option_remove;
typedef struct {
  int m_Timeout;
  const char *m_pDescription;
  const char *m_pReason;
} dd_netmsg_sv_vote_set;
typedef struct {
  int m_Yes;
  int m_No;
  int m_Pass;
  int m_Total;
} dd_netmsg_sv_vote_status;
typedef struct {
  int m_Team;
  const char *m_pMessage;
} dd_netmsg_cl_say;
typedef struct {
  int m_Team;
} dd_netmsg_cl_set_team;
typedef struct {
  int m_SpectatorId;
} dd_netmsg_cl_set_spectator_mode;
typedef struct {
  const char *m_pName;
  const char *m_pClan;
  int m_Country;
  const char *m_pSkin;
  int m_UseCustomColor;
  int m_ColorBody;
  int m_ColorFeet;
} dd_netmsg_cl_start_info;
typedef struct {
  const char *m_pName;
  const char *m_pClan;
  int m_Country;
  const char *m_pSkin;
  int m_UseCustomColor;
  int m_ColorBody;
  int m_ColorFeet;
} dd_netmsg_cl_change_info;
typedef struct {
  int m_Emoticon;
} dd_netmsg_cl_emoticon;
typedef struct {
  int m_Vote;
} dd_netmsg_cl_vote;
typedef struct {
  const char *m_pType;
  const char *m_pValue;
  const char *m_pReason;
} dd_netmsg_cl_call_vote;
typedef struct {
  int m_Time;
  int m_Check;
  int m_Finish;
} dd_netmsg_sv_ddrace_time_legacy;
typedef struct {
  int m_ServerTimeBest;
  int m_PlayerTimeBest;
} dd_netmsg_sv_record_legacy;
typedef struct {
  int m_Show;
} dd_netmsg_cl_show_others_legacy;

/******************************************************************************
 *
 * API
 *
 ******************************************************************************/

typedef struct dd_demo_writer dd_demo_writer;
typedef struct dd_demo_reader dd_demo_reader;
typedef struct dd_snapshot_builder dd_snapshot_builder;

/* Demo Writer API */
dd_demo_writer *demo_w_create();
void demo_w_destroy(dd_demo_writer **dw_ptr);
bool demo_w_begin(dd_demo_writer *dw, FILE *f, const char *map_name, uint32_t map_crc, const char *type);
bool demo_w_write_map(dd_demo_writer *dw, const uint8_t map_sha256[32], const uint8_t *map_data, uint32_t map_size);
bool demo_w_write_snap(dd_demo_writer *dw, int tick, const void *data, int size);
bool demo_w_write_msg(dd_demo_writer *dw, int tick, const void *data, int size);
void demo_w_add_marker(dd_demo_writer *dw, int tick);
bool demo_w_finish(dd_demo_writer *dw);

/* Demo Reader API */
dd_demo_reader *demo_r_create();
void demo_r_destroy(dd_demo_reader **dr_ptr);
bool demo_r_open(dd_demo_reader *dr, FILE *f);
const dd_demo_info *demo_r_get_info(const dd_demo_reader *dr);
bool demo_r_next_chunk(dd_demo_reader *dr, dd_demo_chunk *chunk);
int demo_r_unpack_delta(dd_demo_reader *dr, const void *delta_data, int delta_size, void *unpacked_snap);

/* Snapshot Builder API */
dd_snapshot_builder *demo_sb_create();
void demo_sb_destroy(dd_snapshot_builder **sb_ptr);
void demo_sb_clear(dd_snapshot_builder *sb);
void *demo_sb_add_item(dd_snapshot_builder *sb, int type, int id, int size);
int demo_sb_finish(dd_snapshot_builder *sb, void *snap_data);

#ifdef __cplusplus
}
#endif

#endif /* DDNET_DEMO_H */

#ifdef DDNET_DEMO_IMPLEMENTATION
#undef DDNET_DEMO_IMPLEMENTATION

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/******************************************************************************
 *
 * INTERNAL DEFINITIONS AND HELPER FUNCTIONS
 *
 ******************************************************************************/

/* Internal demo chunk types */
enum {
  DD_CHUNKTYPE_SNAPSHOT = 1,
  DD_CHUNKTYPE_MESSAGE = 2,
  DD_CHUNKTYPE_DELTA = 3,
};

/* Internal chunk header flags */
enum {
  DD_CHUNKTYPEFLAG_TICKMARKER = 0x80,
  DD_CHUNKTICKFLAG_KEYFRAME = 0x40,
  DD_CHUNKTICKFLAG_TICK_COMPRESSED = 0x20,

  DD_CHUNKMASK_TICK = 0x1f,
  DD_CHUNKMASK_TICK_LEGACY = 0x3f,
  DD_CHUNKMASK_TYPE = 0x60,
  DD_CHUNKMASK_SIZE = 0x1f,
};

static const uint8_t DD_HEADER_MARKER[7] = {'T', 'W', 'D', 'E', 'M', 'O', 0};
static const uint8_t DD_SHA256_EXTENSION[16] = {0x6b, 0xe6, 0xda, 0x4a, 0xce, 0xbd, 0x38, 0x0c, 0x9b, 0x5b, 0x12, 0x89, 0xc8, 0x42, 0xd7, 0x80};
static const unsigned char DD_DEMO_VERSION = 6;
static const unsigned char DD_DEMO_VERSION_TICKCOMPRESSION = 5;

// defines for extended types
enum {
  OFFSET_UUID_TYPE = 0x4000,
  MAX_EXTENDED_ITEM_TYPES = 64
};

typedef struct {
  int num_deleted_items;
  int num_update_items;
  int num_temp_items;
  int data[1];
} dd_snap_delta;

/* byte swapping utilities */
static uint32_t dd_be_to_uint(const uint8_t *data) { return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]; }
static void dd_uint_to_be(uint8_t *data, uint32_t val) {
  data[0] = (val >> 24) & 0xFF;
  data[1] = (val >> 16) & 0xFF;
  data[2] = (val >> 8) & 0xFF;
  data[3] = val & 0xFF;
}

/* string utilities */
static void dd_str_timestamp(char *buffer, size_t buffer_size) {
  time_t t = time(NULL);
  struct tm *tmp = localtime(&t);
  if (tmp == NULL) {
    buffer[0] = '\0';
    return;
  }
  strftime(buffer, buffer_size, "%Y-%m-%d %H-%M-%S", tmp);
}

/******************************************************************************
 *
 * UUID MANAGER
 *
 ******************************************************************************/

typedef struct {
  int type_id;
  uint8_t uuid[16];
} dd_uuid_entry;

static const dd_uuid_entry g_dd_uuids[] = {
    {DD_NETOBJTYPE_MYOWNOBJECT, {0x0d, 0xc7, 0x7a, 0x02, 0xbf, 0xee, 0x3a, 0x53, 0xac, 0x8e, 0x0b, 0xb0, 0x24, 0x1b, 0xd7, 0x22}},
    {DD_NETOBJTYPE_DDNETCHARACTER, {0x76, 0xce, 0x45, 0x5b, 0xf9, 0xeb, 0x3a, 0x48, 0xad, 0xd7, 0xe0, 0x4b, 0x94, 0x1d, 0x04, 0x5c}},
    {DD_NETOBJTYPE_DDNETPLAYER, {0x22, 0xca, 0x93, 0x8d, 0x13, 0x80, 0x3e, 0x2b, 0x9e, 0x7b, 0xd2, 0x55, 0x8e, 0xa6, 0xbe, 0x11}},
    {DD_NETOBJTYPE_GAMEINFOEX, {0x93, 0x3d, 0xea, 0x6a, 0xda, 0x79, 0x30, 0xea, 0xa9, 0x8f, 0x8a, 0xf0, 0x36, 0x89, 0xa9, 0x45}},
    {DD_NETOBJTYPE_DDRACEPROJECTILE, {0x0e, 0x6d, 0xb8, 0x5c, 0x2b, 0x61, 0x38, 0x6f, 0xbb, 0xf2, 0xd0, 0xd0, 0x47, 0x1b, 0x92, 0x72}},
    {DD_NETOBJTYPE_DDNETLASER, {0x29, 0xde, 0x68, 0xa2, 0x69, 0x28, 0x31, 0xb8, 0x83, 0x60, 0xa2, 0x30, 0x7e, 0x0d, 0x84, 0x4f}},
    {DD_NETOBJTYPE_DDNETPROJECTILE, {0x65, 0x50, 0xfb, 0xce, 0xf3, 0x17, 0x3b, 0x31, 0x8f, 0xfe, 0xd2, 0xb3, 0x7f, 0x3a, 0xb4, 0x0e}},
    {DD_NETOBJTYPE_DDNETPICKUP, {0xea, 0x5e, 0x4a, 0x51, 0x58, 0xfb, 0x36, 0x84, 0x96, 0xe4, 0xe0, 0xd2, 0x67, 0xf4, 0xca, 0x65}},
    {DD_NETOBJTYPE_DDNETSPECTATORINFO, {0xd1, 0x33, 0x07, 0xb2, 0x9a, 0x19, 0x37, 0xcb, 0x8f, 0x8c, 0x07, 0xc7, 0x18, 0x52, 0x18, 0x83}},
    {DD_NETEVENTTYPE_BIRTHDAY, {0x1f, 0xd3, 0x57, 0x46, 0x62, 0x63, 0x35, 0x8c, 0xb4, 0xd6, 0x6e, 0xf6, 0x0e, 0x0e, 0xfa, 0xaa}},
    {DD_NETEVENTTYPE_FINISH, {0x68, 0xbf, 0x89, 0x39, 0xef, 0x55, 0x38, 0x78, 0x90, 0x82, 0x13, 0x52, 0x7e, 0xb0, 0xa5, 0x97}},
    {DD_NETOBJTYPE_MYOWNEVENT, {0x0c, 0x4f, 0xd2, 0x7d, 0x47, 0xe3, 0x38, 0x71, 0xa2, 0x26, 0x9f, 0x41, 0x74, 0x86, 0xa3, 0x11}},
    {DD_NETOBJTYPE_SPECCHAR, {0x4b, 0x80, 0x1c, 0x74, 0xe2, 0x4c, 0x3c, 0xe0, 0xb9, 0x2c, 0xb7, 0x54, 0xd0, 0x2c, 0xfc, 0x8a}},
    {DD_NETOBJTYPE_SWITCHSTATE, {0xec, 0x15, 0xe6, 0x69, 0xce, 0x11, 0x33, 0x67, 0xae, 0x8e, 0xb9, 0x0e, 0x5b, 0x27, 0xb9, 0xd5}},
    {DD_NETOBJTYPE_ENTITYEX, {0x2d, 0xe9, 0xae, 0xc3, 0x32, 0xe4, 0x39, 0x86, 0x8f, 0x7e, 0xe7, 0x45, 0x9d, 0xa7, 0xf5, 0x35}},
    {DD_NETEVENTTYPE_MAPSOUNDWORLD, {0x54, 0xec, 0xad, 0x2e, 0xbf, 0xad, 0x3b, 0xe5, 0x89, 0x03, 0x62, 0x1b, 0xa0, 0x52, 0x45, 0x8e}}};

static bool dd_uuid_get(int type_id, uint8_t uuid_out[16]) {
  for (size_t i = 0; i < sizeof(g_dd_uuids) / sizeof(g_dd_uuids[0]); ++i) {
    if (g_dd_uuids[i].type_id == type_id) {
      memcpy(uuid_out, g_dd_uuids[i].uuid, 16);
      return true;
    }
  }
  return false;
}

/******************************************************************************
 *
 * COMPRESSION IMPLEMENTATION (VariableInt + Huffman)
 *
 ******************************************************************************/
#define DD_HUFFMAN_EOF_SYMBOL 256
#define DD_HUFFMAN_MAX_SYMBOLS (DD_HUFFMAN_EOF_SYMBOL + 1)
#define DD_HUFFMAN_MAX_NODES (DD_HUFFMAN_MAX_SYMBOLS * 2 - 1)
#define DD_HUFFMAN_LUTBITS 10
#define DD_HUFFMAN_LUTSIZE (1 << DD_HUFFMAN_LUTBITS)
#define DD_HUFFMAN_LUTMASK (DD_HUFFMAN_LUTSIZE - 1)

typedef struct {
  unsigned bits;
  unsigned num_bits;
  unsigned short leafs[2];
  unsigned char symbol;
} dd_huffman_node;

typedef struct {
  dd_huffman_node nodes[DD_HUFFMAN_MAX_NODES];
  dd_huffman_node *decode_lut[DD_HUFFMAN_LUTSIZE];
  dd_huffman_node *start_node;
  int num_nodes;
} dd_huffman_state;

static const unsigned dd_huffman_freq_table[DD_HUFFMAN_MAX_SYMBOLS] = {
    1 << 30, 4545, 2657, 431,  1950, 919, 444, 482, 2244, 617, 838, 542, 715,  1814, 304, 240, 754, 212, 647, 186, 283, 131, 146, 166, 543, 164, 167, 136, 179,
    859,     363,  113,  157,  154,  204, 108, 137, 180,  202, 176, 872, 404,  168,  134, 151, 111, 113, 109, 120, 126, 129, 100, 41,  20,  16,  22,  18,  18,
    17,      19,   16,   37,   13,   21,  362, 166, 99,   78,  95,  88,  81,   70,   83,  284, 91,  187, 77,  68,  52,  68,  59,  66,  61,  638, 71,  157, 50,
    46,      69,   43,   11,   24,   13,  19,  10,  12,   12,  20,  14,  9,    20,   20,  10,  10,  15,  15,  12,  12,  7,   19,  15,  14,  13,  18,  35,  19,
    17,      14,   8,    5,    15,   17,  9,   15,  14,   18,  8,   10,  2173, 134,  157, 68,  188, 60,  170, 60,  194, 62,  175, 71,  148, 67,  167, 78,  211,
    67,      156,  69,   1674, 90,   174, 53,  147, 89,   181, 51,  174, 63,   163,  80,  167, 94,  128, 122, 223, 153, 218, 77,  200, 110, 190, 73,  174, 69,
    145,     66,   277,  143,  141,  60,  136, 53,  180,  57,  142, 57,  158,  61,   166, 112, 152, 92,  26,  22,  21,  28,  20,  26,  30,  21,  32,  27,  20,
    17,      23,   21,   30,   22,   22,  21,  27,  25,   17,  27,  23,  18,   39,   26,  15,  21,  12,  18,  18,  27,  20,  18,  15,  19,  11,  17,  33,  12,
    18,      15,   19,   18,   16,   26,  17,  18,  9,    10,  25,  22,  22,   17,   20,  16,  6,   16,  15,  20,  14,  18,  24,  335, 1517};

typedef struct {
  unsigned short node_id;
  int frequency;
} dd_huffman_construct_node;

static int dd_huffman_compare_nodes(const void *a, const void *b) {
  return (*(const dd_huffman_construct_node **)b)->frequency - (*(const dd_huffman_construct_node **)a)->frequency;
}

static void dd_huffman_setbits_r(dd_huffman_node *nodes, dd_huffman_node *node, int bits, unsigned depth) {
  if (node->leafs[1] != 0xffff) dd_huffman_setbits_r(nodes, &nodes[node->leafs[1]], bits | (1 << depth), depth + 1);
  if (node->leafs[0] != 0xffff) dd_huffman_setbits_r(nodes, &nodes[node->leafs[0]], bits, depth + 1);
  if (node->num_bits) {
    node->bits = bits;
    node->num_bits = depth;
  }
}

static void dd_huffman_init(dd_huffman_state *state) {
  dd_huffman_construct_node nodes_storage[DD_HUFFMAN_MAX_SYMBOLS];
  dd_huffman_construct_node *nodes_left[DD_HUFFMAN_MAX_SYMBOLS];
  int i, k, num_nodes_left = DD_HUFFMAN_MAX_SYMBOLS;

  memset(state, 0, sizeof(*state));

  for (i = 0; i < DD_HUFFMAN_MAX_SYMBOLS; i++) {
    state->nodes[i].num_bits = 0xFFFFFFFF;
    state->nodes[i].symbol = i;
    state->nodes[i].leafs[0] = 0xffff;
    state->nodes[i].leafs[1] = 0xffff;

    nodes_storage[i].frequency = (i == DD_HUFFMAN_EOF_SYMBOL) ? 1 : dd_huffman_freq_table[i];
    nodes_storage[i].node_id = i;
    nodes_left[i] = &nodes_storage[i];
  }

  state->num_nodes = DD_HUFFMAN_MAX_SYMBOLS;
  while (num_nodes_left > 1) {
    qsort(nodes_left, num_nodes_left, sizeof(dd_huffman_construct_node *), dd_huffman_compare_nodes);

    state->nodes[state->num_nodes].num_bits = 0;
    state->nodes[state->num_nodes].leafs[0] = nodes_left[num_nodes_left - 1]->node_id;
    state->nodes[state->num_nodes].leafs[1] = nodes_left[num_nodes_left - 2]->node_id;
    nodes_left[num_nodes_left - 2]->node_id = state->num_nodes;
    nodes_left[num_nodes_left - 2]->frequency += nodes_left[num_nodes_left - 1]->frequency;

    state->num_nodes++;
    num_nodes_left--;
  }

  state->start_node = &state->nodes[state->num_nodes - 1];
  dd_huffman_setbits_r(state->nodes, state->start_node, 0, 0);

  for (i = 0; i < DD_HUFFMAN_LUTSIZE; i++) {
    unsigned bits = i;
    dd_huffman_node *node = state->start_node;
    for (k = 0; k < DD_HUFFMAN_LUTBITS; k++) {
      node = &state->nodes[node->leafs[bits & 1]];
      bits >>= 1;
      if (!node || node->num_bits) {
        state->decode_lut[i] = node;
        break;
      }
    }
    if (k == DD_HUFFMAN_LUTBITS) state->decode_lut[i] = node;
  }
}

static int dd_huffman_compress(const dd_huffman_state *state, const void *input, int input_size, void *output, int output_size) {
  const uint8_t *src = (const uint8_t *)input;
  uint8_t *dst = (uint8_t *)output;
  const uint8_t *dst_end = dst + output_size;
  uint32_t bits = 0;
  unsigned bit_count = 0;

  if (input_size) {
    while (input_size--) {
      const dd_huffman_node *node = &state->nodes[*src++];
      bits |= node->bits << bit_count;
      bit_count += node->num_bits;
      while (bit_count >= 8) {
        if (dst >= dst_end) return -1;
        *dst++ = bits & 0xff;
        bits >>= 8;
        bit_count -= 8;
      }
    }
  }

  const dd_huffman_node *node = &state->nodes[DD_HUFFMAN_EOF_SYMBOL];
  bits |= node->bits << bit_count;
  bit_count += node->num_bits;
  while (bit_count >= 8) {
    if (dst >= dst_end) return -1;
    *dst++ = bits & 0xff;
    bits >>= 8;
    bit_count -= 8;
  }

  if (dst >= dst_end) return -1;
  *dst++ = bits;
  return (int)(dst - (uint8_t *)output);
}

static int dd_huffman_decompress(const dd_huffman_state *state, const void *input, int input_size, void *output, int output_size) {
  const uint8_t *src = (const uint8_t *)input;
  const uint8_t *src_end = src + input_size;
  uint8_t *dst = (uint8_t *)output;
  uint8_t *dst_end = dst + output_size;
  uint32_t bits = 0;
  unsigned bit_count = 0;
  const dd_huffman_node *eof_node = &state->nodes[DD_HUFFMAN_EOF_SYMBOL];

  while (1) {
    const dd_huffman_node *node = NULL;
    if (bit_count >= DD_HUFFMAN_LUTBITS) node = state->decode_lut[bits & DD_HUFFMAN_LUTMASK];

    while (bit_count < 24 && src != src_end) {
      bits |= (*src++) << bit_count;
      bit_count += 8;
    }

    if (!node) node = state->decode_lut[bits & DD_HUFFMAN_LUTMASK];

    if (!node) return -1;

    if (node->num_bits) {
      bits >>= node->num_bits;
      bit_count -= node->num_bits;
    } else {
      bits >>= DD_HUFFMAN_LUTBITS;
      bit_count -= DD_HUFFMAN_LUTBITS;
      while (1) {
        node = &state->nodes[node->leafs[bits & 1]];
        bit_count--;
        bits >>= 1;
        if (node->num_bits) break;
        if (bit_count == 0) return -1;
      }
    }

    if (node == eof_node) break;

    if (dst == dst_end) return -1;
    *dst++ = node->symbol;
  }
  return (int)(dst - (uint8_t *)output);
}

static uint8_t *dd_variable_int_pack(uint8_t *dst, int i, int dst_size) {
  if (dst_size <= 0) return NULL;
  dst_size--;
  *dst = 0;
  if (i < 0) {
    *dst |= 0x40;
    i = ~i;
  }
  *dst |= i & 0x3F;
  i >>= 6;
  while (i) {
    if (dst_size <= 0) return NULL;
    *dst |= 0x80;
    dst_size--;
    dst++;
    *dst = i & 0x7F;
    i >>= 7;
  }
  dst++;
  return dst;
}

static const uint8_t *dd_variable_int_unpack(const uint8_t *src, int *val, int src_size) {
  if (src_size <= 0) return NULL;
  int sign = (*src >> 6) & 1;
  *val = *src & 0x3F;
  src_size--;

  static const int masks[] = {0x7F, 0x7F, 0x7F, 0x0F};
  static const int shifts[] = {6, 13, 20, 27};

  for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
    if (!(*src & 0x80)) break;
    if (src_size <= 0) return NULL;
    src_size--;
    src++;
    *val |= (*src & masks[i]) << shifts[i];
  }

  src++;
  *val ^= -sign;
  return src;
}

static long dd_variable_int_compress(const void *src, int src_size, void *dst, int dst_size) {
  const int *p_src = (const int *)src;
  uint8_t *p_dst = (uint8_t *)dst;
  const uint8_t *p_dst_end = p_dst + dst_size;
  int num_ints = src_size / sizeof(int);

  while (num_ints--) {
    p_dst = dd_variable_int_pack(p_dst, *p_src++, p_dst_end - p_dst);
    if (!p_dst) return -1;
  }
  return (long)(p_dst - (uint8_t *)dst);
}

static long dd_variable_int_decompress(const void *src, int src_size, void *dst, int dst_size) {
  const uint8_t *p_src = (const uint8_t *)src;
  const uint8_t *p_src_end = p_src + src_size;
  int *p_dst = (int *)dst;
  const int *p_dst_end = p_dst + (dst_size / sizeof(int));

  while (p_src < p_src_end) {
    if (p_dst >= p_dst_end) return -1;
    p_src = dd_variable_int_unpack(p_src, p_dst, p_src_end - p_src);
    if (!p_src) return -1;
    p_dst++;
  }
  return (long)((uint8_t *)p_dst - (uint8_t *)dst);
}

static int dd_data_compress(dd_huffman_state *huff, const void *data, int size, void *output, int output_size) {
  uint8_t intpack_buf[DD_MAX_PAYLOAD];
  int intpack_size = dd_variable_int_compress(data, size, intpack_buf, sizeof(intpack_buf));
  if (intpack_size < 0) return -1;
  return dd_huffman_compress(huff, intpack_buf, intpack_size, output, output_size);
}

static int dd_data_decompress(dd_huffman_state *huff, const void *data, int size, void *output, int output_size) {
  uint8_t intpack_buf[DD_MAX_PAYLOAD];
  int intpack_size = dd_huffman_decompress(huff, data, size, intpack_buf, sizeof(intpack_buf));
  if (intpack_size < 0) return -1;
  return dd_variable_int_decompress(intpack_buf, intpack_size, output, output_size);
}

/******************************************************************************
 *
 * SNAPSHOT IMPLEMENTATION
 *
 ******************************************************************************/
int dd_snap_get_item_size(const dd_snapshot *snap, int index) {
  if (index < 0 || index >= snap->num_items) return 0;
  if (index == snap->num_items - 1) return (snap->data_size - dd_snap_offsets(snap)[index]) - sizeof(dd_snap_item);
  return (dd_snap_offsets(snap)[index + 1] - dd_snap_offsets(snap)[index]) - sizeof(dd_snap_item);
}

const dd_snap_item *dd_snap_find_item(const dd_snapshot *snap, int type, int id) {
  for (int i = 0; i < snap->num_items; i++) {
    const dd_snap_item *item = dd_snap_get_item(snap, i);
    if (dd_snap_item_type(item) == type && dd_snap_item_id(item) == id) {
      return item;
    }
  }
  return NULL;
}

struct dd_snapshot_builder {
  uint8_t data[DD_MAX_SNAPSHOT_SIZE];
  int data_size;
  int offsets[DD_MAX_SNAPSHOT_ITEMS];
  int num_items;

  int extended_item_types[MAX_EXTENDED_ITEM_TYPES];
  int num_extended_item_types;
};

static int demo_sb_get_extended_item_type_index(dd_snapshot_builder *sb, int type_id, bool *is_new) {
  *is_new = false;
  for (int i = 0; i < sb->num_extended_item_types; i++) {
    if (sb->extended_item_types[i] == type_id) return i;
  }

  if (sb->num_extended_item_types >= MAX_EXTENDED_ITEM_TYPES) return -1;

  int index = sb->num_extended_item_types++;
  sb->extended_item_types[index] = type_id;
  *is_new = true;
  return index;
}

dd_snapshot_builder *demo_sb_create() {
  dd_snapshot_builder *sb = (dd_snapshot_builder *)malloc(sizeof(dd_snapshot_builder));
  if (sb) demo_sb_clear(sb);
  return sb;
}

void demo_sb_destroy(dd_snapshot_builder **sb_ptr) {
  if (sb_ptr && *sb_ptr) {
    free(*sb_ptr);
    *sb_ptr = NULL;
  }
}

void demo_sb_clear(dd_snapshot_builder *sb) {
  sb->data_size = 0;
  sb->num_items = 0;
  sb->num_extended_item_types = 0;
}

void *demo_sb_add_item(dd_snapshot_builder *sb, int type, int id, int size) {
  if (sb->num_items >= DD_MAX_SNAPSHOT_ITEMS || sb->data_size + (int)sizeof(dd_snap_item) + size > DD_MAX_SNAPSHOT_SIZE) {
    return NULL;
  }

  int final_type = type;

  if (type >= OFFSET_UUID) {
    bool is_new = false;
    int extended_index = demo_sb_get_extended_item_type_index(sb, type, &is_new);
    if (extended_index == -1) {
      return NULL;
    }

    if (is_new) {
      if (sb->num_items >= DD_MAX_SNAPSHOT_ITEMS || sb->data_size + (int)sizeof(dd_snap_item) + 16 > DD_MAX_SNAPSHOT_SIZE) {
        sb->num_extended_item_types--;
        return NULL;
      }

      int internal_id = DD_MAX_TYPE - extended_index;
      dd_snap_item *ex_item = (dd_snap_item *)(sb->data + sb->data_size);
      ex_item->type_and_id = (DD_NETOBJTYPE_EX << 16) | internal_id;
      sb->offsets[sb->num_items] = sb->data_size;
      sb->data_size += sizeof(dd_snap_item) + 16;
      sb->num_items++;

      int *uuid_data = dd_snap_item_data(ex_item);
      uint8_t uuid[16];
      if (dd_uuid_get(type, uuid)) {
        uuid_data[0] = dd_be_to_uint(uuid);
        uuid_data[1] = dd_be_to_uint(uuid + 4);
        uuid_data[2] = dd_be_to_uint(uuid + 8);
        uuid_data[3] = dd_be_to_uint(uuid + 12);
      } else {
        memset(uuid_data, 0, 16);
      }
    }

    final_type = DD_MAX_TYPE - extended_index;
  }

  dd_snap_item *obj = (dd_snap_item *)(sb->data + sb->data_size);
  obj->type_and_id = (final_type << 16) | id;
  sb->offsets[sb->num_items] = sb->data_size;
  sb->data_size += sizeof(dd_snap_item) + size;
  sb->num_items++;

  void *p_data = (void *)dd_snap_item_data(obj);
  memset(p_data, 0, size);
  return p_data;
}

int demo_sb_finish(dd_snapshot_builder *sb, void *snap_data) {
  dd_snapshot *snap = (dd_snapshot *)snap_data;
  snap->data_size = sb->data_size;
  snap->num_items = sb->num_items;

  size_t total_size = sizeof(dd_snapshot) + sizeof(int) * sb->num_items + sb->data_size;
  if (total_size > DD_MAX_SNAPSHOT_SIZE) return -1;

  memcpy(dd_snap_offsets(snap), sb->offsets, sizeof(int) * sb->num_items);
  memcpy(dd_snap_data_start(snap), sb->data, sb->data_size);

  return (int)total_size;
}

/******************************************************************************
 *
 * DEMO WRITER IMPLEMENTATION
 *
 ******************************************************************************/

struct dd_demo_writer {
  FILE *file;
  int last_tick_marker;
  int first_tick;
  int last_keyframe;
  uint8_t last_snapshot_data[DD_MAX_SNAPSHOT_SIZE];
  int timeline_markers[DD_MAX_TIMELINE_MARKERS];
  int num_timeline_markers;
  dd_huffman_state huffman;
  short item_sizes[DD_MAX_NETOBJSIZES];
};

static void dd_writer_init_netobj_sizes(dd_demo_writer *dw);

dd_demo_writer *demo_w_create() {
  dd_demo_writer *dw = (dd_demo_writer *)calloc(1, sizeof(dd_demo_writer));
  if (!dw) return NULL;
  dd_huffman_init(&dw->huffman);
  dd_writer_init_netobj_sizes(dw);
  return dw;
}

void demo_w_destroy(dd_demo_writer **dw_ptr) {
  if (dw_ptr && *dw_ptr) {
    if ((*dw_ptr)->file) demo_w_finish(*dw_ptr);
    free(*dw_ptr);
    *dw_ptr = NULL;
  }
}

bool demo_w_begin(dd_demo_writer *dw, FILE *f, const char *map_name, uint32_t map_crc, const char *type) {
  if (!dw || !f) return false;

  dw->file = f;
  dw->last_tick_marker = -1;
  dw->first_tick = -1;
  dw->last_keyframe = -1;
  dw->num_timeline_markers = 0;
  memset(dw->last_snapshot_data, 0, sizeof(dw->last_snapshot_data));

  dd_demo_header header;
  memset(&header, 0, sizeof(header));
  memcpy(header.marker, DD_HEADER_MARKER, sizeof(DD_HEADER_MARKER));
  header.version = DD_DEMO_VERSION;
  strncpy(header.net_version, "0.6 626fce9a778df4d4", sizeof(header.net_version) - 1);
  strncpy(header.map_name, map_name, sizeof(header.map_name) - 1);
  dd_uint_to_be(header.map_crc, map_crc);
  strncpy(header.type, type, sizeof(header.type) - 1);
  dd_str_timestamp(header.timestamp, sizeof(header.timestamp));

  fwrite(&header, sizeof(header), 1, f);

  dd_timeline_markers markers;
  memset(&markers, 0, sizeof(markers));
  fwrite(&markers, sizeof(markers), 1, f);

  return true;
}

bool demo_w_write_map(dd_demo_writer *dw, const uint8_t map_sha256[32], const uint8_t *map_data, uint32_t map_size) {
  if (!dw || !dw->file) return false;

  long current_pos = ftell(dw->file);
  fseek(dw->file, offsetof(dd_demo_header, map_size), SEEK_SET);
  uint8_t map_size_be[4];
  dd_uint_to_be(map_size_be, map_size);
  fwrite(map_size_be, sizeof(map_size_be), 1, dw->file);
  fseek(dw->file, current_pos, SEEK_SET);

  fwrite(DD_SHA256_EXTENSION, sizeof(DD_SHA256_EXTENSION), 1, dw->file);
  fwrite(map_sha256, 32, 1, dw->file);
  if (map_size > 0) fwrite(map_data, map_size, 1, dw->file);

  return true;
}

static void demo_w_write_chunk_header(dd_demo_writer *dw, int type, int size) {
  uint8_t chunk_header[3];
  chunk_header[0] = ((type & 0x3) << 5);

  if (size < 30) {
    chunk_header[0] |= size;
    fwrite(chunk_header, 1, 1, dw->file);
  } else if (size < 256) {
    chunk_header[0] |= 30;
    chunk_header[1] = size & 0xff;
    fwrite(chunk_header, 2, 1, dw->file);
  } else {
    chunk_header[0] |= 31;
    chunk_header[1] = size & 0xff;
    chunk_header[2] = size >> 8;
    fwrite(chunk_header, 3, 1, dw->file);
  }
}

static void demo_w_write_data(dd_demo_writer *dw, int type, const void *data, int size) {
  uint8_t compressed_buf[DD_MAX_PAYLOAD];
  uint8_t *padded_data = NULL;

  int padded_size = size;
  if (size % 4 != 0) {
    padded_size = size + (4 - (size % 4));
  }

  padded_data = (uint8_t *)malloc(padded_size);
  if (!padded_data) return;
  memcpy(padded_data, data, size);
  memset(padded_data + size, 0, padded_size - size);

  int compressed_size = dd_data_compress(&dw->huffman, padded_data, padded_size, compressed_buf, sizeof(compressed_buf));
  free(padded_data);

  if (compressed_size < 0) return;

  demo_w_write_chunk_header(dw, type, compressed_size);
  fwrite(compressed_buf, compressed_size, 1, dw->file);
}

static void demo_w_write_tickmarker(dd_demo_writer *dw, int tick, bool keyframe) {
  if (dw->last_tick_marker == -1 || tick - dw->last_tick_marker > DD_CHUNKMASK_TICK || keyframe) {
    uint8_t chunk[5];
    chunk[0] = DD_CHUNKTYPEFLAG_TICKMARKER;
    if (keyframe) chunk[0] |= DD_CHUNKTICKFLAG_KEYFRAME;
    dd_uint_to_be(chunk + 1, tick);
    fwrite(chunk, 5, 1, dw->file);
  } else {
    uint8_t chunk = DD_CHUNKTYPEFLAG_TICKMARKER | DD_CHUNKTICKFLAG_TICK_COMPRESSED | (tick - dw->last_tick_marker);
    fwrite(&chunk, 1, 1, dw->file);
  }
  dw->last_tick_marker = tick;
  if (dw->first_tick < 0) dw->first_tick = tick;
}

static int diff_item(const int *past, const int *current, int *out, int size) {
  int needed = 0;
  while (size--) {
    *out = (unsigned int)*current - (unsigned int)*past;
    needed |= *out;
    out++;
    past++;
    current++;
  }
  return needed;
}

bool demo_w_write_snap(dd_demo_writer *dw, int tick, const void *data, int size) {
  if (!dw || !dw->file) return false;

  if (dw->last_keyframe == -1 || (tick - dw->last_keyframe) > DD_SERVER_TICK_SPEED * 5) {
    demo_w_write_tickmarker(dw, tick, true);
    demo_w_write_data(dw, DD_CHUNKTYPE_SNAPSHOT, data, size);
    dw->last_keyframe = tick;
    memcpy(dw->last_snapshot_data, data, size);
  } else {
    demo_w_write_tickmarker(dw, tick, false);

    dd_snapshot *from = (dd_snapshot *)dw->last_snapshot_data;
    dd_snapshot *to = (dd_snapshot *)data;
    uint8_t delta_buf[DD_MAX_SNAPSHOT_SIZE];
    dd_snap_delta *delta = (dd_snap_delta *)delta_buf;
    int *delta_data = delta->data;

    delta->num_deleted_items = 0;
    delta->num_update_items = 0;
    delta->num_temp_items = 0;

    for (int i = 0; i < from->num_items; i++) {
      const dd_snap_item *from_item = dd_snap_get_item(from, i);
      if (!dd_snap_find_item(to, dd_snap_item_type(from_item), dd_snap_item_id(from_item))) {
        delta->num_deleted_items++;
        *delta_data++ = dd_snap_item_key(from_item);
      }
    }

    for (int i = 0; i < to->num_items; i++) {
      const dd_snap_item *to_item = dd_snap_get_item(to, i);
      int item_type = dd_snap_item_type(to_item);
      int item_id = dd_snap_item_id(to_item);
      int item_size = dd_snap_get_item_size(to, i);
      const dd_snap_item *from_item = dd_snap_find_item(from, item_type, item_id);

      bool include_size = item_type >= DD_MAX_NETOBJSIZES || dw->item_sizes[item_type] == 0;

      if (from_item) {
        int *diff_storage = (int *)malloc(item_size);
        if (diff_storage && diff_item(dd_snap_item_data(from_item), dd_snap_item_data(to_item), diff_storage, item_size / 4)) {
          *delta_data++ = item_type;
          *delta_data++ = item_id;
          if (include_size) *delta_data++ = item_size / 4;
          memcpy(delta_data, diff_storage, item_size);
          delta_data += item_size / 4;
          delta->num_update_items++;
        }
        if (diff_storage) free(diff_storage);
      } else {
        *delta_data++ = item_type;
        *delta_data++ = item_id;
        if (include_size) *delta_data++ = item_size / 4;
        memcpy(delta_data, dd_snap_item_data(to_item), item_size);
        delta_data += item_size / 4;
        delta->num_update_items++;
      }
    }

    int delta_size = (int)((uint8_t *)delta_data - delta_buf);
    if (delta_size > (int)sizeof(dd_snap_delta) - (int)sizeof(int)) {
      demo_w_write_data(dw, DD_CHUNKTYPE_DELTA, delta_buf, delta_size);
    }
    memcpy(dw->last_snapshot_data, data, size);
  }
  return true;
}

bool demo_w_write_msg(dd_demo_writer *dw, int tick, const void *data, int size) {
  if (!dw || !dw->file) return false;
  if (dw->last_tick_marker != tick) {
    demo_w_write_tickmarker(dw, tick, false);
  }
  demo_w_write_data(dw, DD_CHUNKTYPE_MESSAGE, data, size);
  return true;
}

void demo_w_add_marker(dd_demo_writer *dw, int tick) {
  if (dw->num_timeline_markers < DD_MAX_TIMELINE_MARKERS) {
    dw->timeline_markers[dw->num_timeline_markers++] = tick;
  }
}

bool demo_w_finish(dd_demo_writer *dw) {
  if (!dw || !dw->file) return false;

  fseek(dw->file, offsetof(dd_demo_header, length), SEEK_SET);
  uint8_t length_be[4];
  int length = dw->first_tick == -1 ? 0 : (dw->last_tick_marker - dw->first_tick) / DD_SERVER_TICK_SPEED;
  dd_uint_to_be(length_be, length);
  fwrite(length_be, sizeof(length_be), 1, dw->file);

  fseek(dw->file, sizeof(dd_demo_header), SEEK_SET);
  uint8_t num_markers_be[4];
  dd_uint_to_be(num_markers_be, dw->num_timeline_markers);
  fwrite(num_markers_be, sizeof(num_markers_be), 1, dw->file);

  for (int i = 0; i < dw->num_timeline_markers; i++) {
    uint8_t marker_be[4];
    dd_uint_to_be(marker_be, dw->timeline_markers[i]);
    fwrite(marker_be, sizeof(marker_be), 1, dw->file);
  }

  fclose(dw->file);
  dw->file = NULL;
  return true;
}

/******************************************************************************
 *
 * DEMO READER IMPLEMENTATION
 *
 ******************************************************************************/

struct dd_demo_reader {
  FILE *file;
  dd_demo_info info;
  int current_tick;
  uint8_t chunk_data[DD_MAX_PAYLOAD];
  uint8_t last_snapshot_data[DD_MAX_SNAPSHOT_SIZE];
  dd_huffman_state huffman;
  short item_sizes[DD_MAX_NETOBJSIZES];
};

static void dd_reader_init_netobj_sizes(dd_demo_reader *dr);

dd_demo_reader *demo_r_create() {
  dd_demo_reader *dr = (dd_demo_reader *)calloc(1, sizeof(dd_demo_reader));
  if (!dr) return NULL;
  dd_huffman_init(&dr->huffman);
  dd_reader_init_netobj_sizes(dr);
  return dr;
}

void demo_r_destroy(dd_demo_reader **dr_ptr) {
  if (dr_ptr && *dr_ptr) {
    if ((*dr_ptr)->file) fclose((*dr_ptr)->file);
    free(*dr_ptr);
    *dr_ptr = NULL;
  }
}

bool demo_r_open(dd_demo_reader *dr, FILE *f) {
  if (!dr || !f) return false;

  dr->file = f;
  dr->current_tick = -1;

  if (fread(&dr->info.header, sizeof(dd_demo_header), 1, f) != 1) return false;
  if (memcmp(dr->info.header.marker, DD_HEADER_MARKER, sizeof(DD_HEADER_MARKER)) != 0) return false;

  dr->info.map_size = dd_be_to_uint(dr->info.header.map_size);
  dr->info.map_crc = dd_be_to_uint(dr->info.header.map_crc);
  dr->info.length = dd_be_to_uint(dr->info.header.length);

  if (dr->info.header.version > 3) {
    if (fread(&dr->info.timeline_markers_raw, sizeof(dd_timeline_markers), 1, f) != 1) return false;
    dr->info.num_markers = dd_be_to_uint(dr->info.timeline_markers_raw.num_markers);
    if (dr->info.num_markers > DD_MAX_TIMELINE_MARKERS) dr->info.num_markers = DD_MAX_TIMELINE_MARKERS;
    for (int i = 0; i < dr->info.num_markers; i++) {
      dr->info.markers[i] = dd_be_to_uint(dr->info.timeline_markers_raw.markers[i]);
    }
  }

  uint8_t uuid[16];
  size_t read_bytes = fread(uuid, 1, sizeof(uuid), f);
  if (read_bytes == sizeof(uuid) && memcmp(uuid, DD_SHA256_EXTENSION, sizeof(uuid)) == 0) {
    dr->info.has_sha256 = fread(dr->info.map_sha256, 32, 1, f) == 1;
  } else {
    dr->info.has_sha256 = false;
    fseek(f, -(long)read_bytes, SEEK_CUR);
  }

  fseek(f, dr->info.map_size, SEEK_CUR);

  return true;
}

const dd_demo_info *demo_r_get_info(const dd_demo_reader *dr) { return &dr->info; }

bool demo_r_next_chunk(dd_demo_reader *dr, dd_demo_chunk *chunk) {
  uint8_t header_byte;

  while (fread(&header_byte, 1, 1, dr->file) == 1) {
    if (header_byte & DD_CHUNKTYPEFLAG_TICKMARKER) {
      chunk->is_keyframe = (header_byte & DD_CHUNKTICKFLAG_KEYFRAME) != 0;
      if (dr->info.header.version >= DD_DEMO_VERSION_TICKCOMPRESSION && (header_byte & DD_CHUNKTICKFLAG_TICK_COMPRESSED)) {
        if (dr->current_tick == -1) dr->current_tick = 0; // Should not happen on well-formed demos
        dr->current_tick += header_byte & DD_CHUNKMASK_TICK;
      } else {
        uint8_t tick_data[4];
        if (fread(tick_data, sizeof(tick_data), 1, dr->file) != 1) return false;
        dr->current_tick = dd_be_to_uint(tick_data);
      }
      chunk->type = DD_CHUNK_TICK_MARKER;
      chunk->tick = dr->current_tick;
      chunk->size = 0;
      chunk->data = NULL;
      return true;
    }

    int type = (header_byte & DD_CHUNKMASK_TYPE) >> 5;
    int size = header_byte & DD_CHUNKMASK_SIZE;

    if (size == 30) {
      uint8_t size_byte;
      if (fread(&size_byte, 1, 1, dr->file) != 1) return false;
      size = size_byte;
    } else if (size == 31) {
      uint8_t size_bytes[2];
      if (fread(size_bytes, 2, 1, dr->file) != 1) return false;
      size = (size_bytes[1] << 8) | size_bytes[0];
    }

    uint8_t compressed_data[DD_MAX_PAYLOAD];
    if ((int)fread(compressed_data, 1, size, dr->file) != size) return false;

    int decompressed_size = dd_data_decompress(&dr->huffman, compressed_data, size, dr->chunk_data, sizeof(dr->chunk_data));
    if (decompressed_size < 0) return false;

    chunk->tick = dr->current_tick;
    chunk->is_keyframe = false;
    chunk->size = decompressed_size;
    chunk->data = dr->chunk_data;

    switch (type) {
    case DD_CHUNKTYPE_SNAPSHOT:
      chunk->type = DD_CHUNK_SNAP;
      memcpy(dr->last_snapshot_data, chunk->data, chunk->size);
      break;
    case DD_CHUNKTYPE_DELTA:
      chunk->type = DD_CHUNK_SNAP_DELTA;
      break;
    case DD_CHUNKTYPE_MESSAGE:
      chunk->type = DD_CHUNK_MSG;
      break;
    default:
      continue;
    }
    return true;
  }
  return false;
}

static void undiff_item(const int *past, const int *diff, int *out, int size) {
  while (size--) {
    *out++ = (unsigned int)*past++ + (unsigned int)*diff++;
  }
}

int demo_r_unpack_delta(dd_demo_reader *dr, const void *delta_data, int delta_size, void *unpacked_snap_data) {
  dd_snap_delta *delta = (dd_snap_delta *)delta_data;
  dd_snapshot *from = (dd_snapshot *)dr->last_snapshot_data;
  dd_snapshot_builder *sb = demo_sb_create();

  const int *deleted_items = delta->data;

  for (int i = 0; i < from->num_items; i++) {
    const dd_snap_item *from_item = dd_snap_get_item(from, i);
    bool keep = true;
    for (int d = 0; d < delta->num_deleted_items; d++) {
      if (deleted_items[d] == dd_snap_item_key(from_item)) {
        keep = false;
        break;
      }
    }
    if (keep) {
      int item_size = dd_snap_get_item_size(from, i);
      void *obj = demo_sb_add_item(sb, dd_snap_item_type(from_item), dd_snap_item_id(from_item), item_size);
      if (obj) memcpy(obj, dd_snap_item_data(from_item), item_size);
    }
  }

  const int *updated_items = deleted_items + delta->num_deleted_items;
  for (int i = 0; i < delta->num_update_items; i++) {
    int type = *updated_items++;
    int id = *updated_items++;
    int item_size;

    if (type >= 0 && type < DD_MAX_NETOBJSIZES && dr->item_sizes[type]) {
      item_size = dr->item_sizes[type];
    } else {
      item_size = (*updated_items++) * sizeof(int);
    }

    const dd_snap_item *from_item = dd_snap_find_item(from, type, id);
    void *new_data = demo_sb_add_item(sb, type, id, item_size);
    if (!new_data) continue;

    if (from_item) {
      undiff_item(dd_snap_item_data(from_item), updated_items, (int *)new_data, item_size / 4);
    } else {
      memcpy(new_data, updated_items, item_size);
    }
    updated_items += item_size / 4;
  }

  int final_size = demo_sb_finish(sb, unpacked_snap_data);
  demo_sb_destroy(&sb);

  if (final_size > 0) {
    memcpy(dr->last_snapshot_data, unpacked_snap_data, final_size);
  }

  return final_size;
}

static void dd_init_netobj_sizes(short *item_sizes) {
  memset(item_sizes, 0, sizeof(short) * DD_MAX_NETOBJSIZES);
  item_sizes[DD_NETOBJTYPE_PLAYERINPUT] = sizeof(dd_netobj_player_input);
  item_sizes[DD_NETOBJTYPE_PROJECTILE] = sizeof(dd_netobj_projectile);
  item_sizes[DD_NETOBJTYPE_LASER] = sizeof(dd_netobj_laser);
  item_sizes[DD_NETOBJTYPE_PICKUP] = sizeof(dd_netobj_pickup);
  item_sizes[DD_NETOBJTYPE_FLAG] = sizeof(dd_netobj_flag);
  item_sizes[DD_NETOBJTYPE_GAMEINFO] = sizeof(dd_netobj_game_info);
  item_sizes[DD_NETOBJTYPE_GAMEDATA] = sizeof(dd_netobj_game_data);
  item_sizes[DD_NETOBJTYPE_CHARACTERCORE] = sizeof(dd_netobj_character_core);
  item_sizes[DD_NETOBJTYPE_CHARACTER] = sizeof(dd_netobj_character);
  item_sizes[DD_NETOBJTYPE_PLAYERINFO] = sizeof(dd_netobj_player_info);
  item_sizes[DD_NETOBJTYPE_CLIENTINFO] = sizeof(dd_netobj_client_info);
  item_sizes[DD_NETOBJTYPE_SPECTATORINFO] = sizeof(dd_netobj_spectator_info);
  item_sizes[DD_NETEVENTTYPE_COMMON] = sizeof(dd_netevent_common);
  item_sizes[DD_NETEVENTTYPE_EXPLOSION] = sizeof(dd_netevent_explosion);
  item_sizes[DD_NETEVENTTYPE_SPAWN] = sizeof(dd_netevent_spawn);
  item_sizes[DD_NETEVENTTYPE_HAMMERHIT] = sizeof(dd_netevent_hammer_hit);
  item_sizes[DD_NETEVENTTYPE_DEATH] = sizeof(dd_netevent_death);
  item_sizes[DD_NETEVENTTYPE_SOUNDGLOBAL] = sizeof(dd_netevent_sound_global);
  item_sizes[DD_NETEVENTTYPE_SOUNDWORLD] = sizeof(dd_netevent_sound_world);
  item_sizes[DD_NETEVENTTYPE_DAMAGEIND] = sizeof(dd_netevent_damage_ind);
}

static void dd_writer_init_netobj_sizes(dd_demo_writer *dw) { dd_init_netobj_sizes(dw->item_sizes); }

static void dd_reader_init_netobj_sizes(dd_demo_reader *dr) { dd_init_netobj_sizes(dr->item_sizes); }

#endif /* DDNET_DEMO_IMPLEMENTATION */
