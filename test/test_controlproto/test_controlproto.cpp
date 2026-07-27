/*
 * Native unit tests for ControlProto against the authoritative wire-contract
 * golden vectors (docs/superpowers/specs/2026-06-15-control-plane-contract.md).
 *
 * Each golden vector from §1.5 (UDP) and §2.2 (REST) is asserted exactly:
 * encoders must produce the exact strings (minus the hostname prefix TagNet
 * adds), the CTL parser must yield the exact structs, garbage lines must drop
 * safely, config round-trips, and unknown PATCH fields are rejected.
 */

#include <unity.h>

#include <cstring>

#include <ControlProto.h>

using namespace ControlProto;

void setUp() {}
void tearDown() {}

// --- §1.5 Encoders: heartbeat ----------------------------------------------

void test_format_heartbeat_golden() {
  char buf[160];
  const int n = formatHeartbeat(buf, sizeof(buf), "a1b2c3", "192.168.1.24",
                                "2.0.0", 2, "team-colours", 100);
  TEST_ASSERT_EQUAL_STRING(
      "HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100 "
      "online=1",
      buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

// --- §1.5 Encoders: hit event ----------------------------------------------

void test_format_hit_event_golden() {
  char buf[160];
  formatHitEvent(buf, sizeof(buf), "a1b2c3", 2, 2, "vatos", 80, 12345);
  TEST_ASSERT_EQUAL_STRING(
      "EVT hit victim=a1b2c3 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345",
      buf);
}

// --- §1.5 Encoders: state event (hp omitted vs present) --------------------

void test_format_state_dead_omits_hp() {
  char buf[160];
  // s=dead carries NO hp: pass a negative hp to omit the token.
  formatStateEvent(buf, sizeof(buf), "dead", -1, 12500);
  TEST_ASSERT_EQUAL_STRING("EVT state s=dead ts=12500", buf);
}

void test_format_state_respawn_includes_hp() {
  char buf[160];
  formatStateEvent(buf, sizeof(buf), "respawn", 100, 20000);
  TEST_ASSERT_EQUAL_STRING("EVT state s=respawn hp=100 ts=20000", buf);
}

void test_format_state_ready_and_idle() {
  char buf[160];
  formatStateEvent(buf, sizeof(buf), "ready", 100, 5);
  TEST_ASSERT_EQUAL_STRING("EVT state s=ready hp=100 ts=5", buf);
  formatStateEvent(buf, sizeof(buf), "idle", -1, 7);
  TEST_ASSERT_EQUAL_STRING("EVT state s=idle ts=7", buf);
}

void test_format_state_stunned_includes_hp() {
  char buf[160];
  // Post-hit cooldown while still alive: s=stunned carries the surviving hp
  // (distinct from s=dead at hp==0).
  formatStateEvent(buf, sizeof(buf), "stunned", 93, 977127);
  TEST_ASSERT_EQUAL_STRING("EVT state s=stunned hp=93 ts=977127", buf);
}

// --- §1.5 CTL parser golden vectors ----------------------------------------

void test_parse_ctl_start_with_ts() {
  Control c;
  TEST_ASSERT_TRUE(parseControl("CTL start ts=30000", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Start, (int)c.kind);
  TEST_ASSERT_TRUE(c.hasTs);
  TEST_ASSERT_EQUAL_UINT32(30000u, c.ts);
  TEST_ASSERT_FALSE(c.hasHp);
}

void test_parse_ctl_start_without_ts() {
  Control c;
  TEST_ASSERT_TRUE(parseControl("CTL start", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Start, (int)c.kind);
  TEST_ASSERT_FALSE(c.hasTs);
}

void test_parse_ctl_stop() {
  Control c;
  TEST_ASSERT_TRUE(parseControl("CTL stop", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Stop, (int)c.kind);
  TEST_ASSERT_FALSE(c.hasTs);
  TEST_ASSERT_FALSE(c.hasHp);
}

void test_parse_ctl_reset_with_hp() {
  Control c;
  TEST_ASSERT_TRUE(parseControl("CTL reset hp=100", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Reset, (int)c.kind);
  TEST_ASSERT_TRUE(c.hasHp);
  TEST_ASSERT_EQUAL_INT(100, c.hp);
}

void test_parse_ctl_reset_without_hp() {
  Control c;
  TEST_ASSERT_TRUE(parseControl("CTL reset", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Reset, (int)c.kind);
  TEST_ASSERT_FALSE(c.hasHp);
}

// --- §1.5 garbage / partial lines must drop (no throw) ---------------------

void test_parse_garbage_drops() {
  Control c;
  // Hostname-prefixed telemetry (our own/others' broadcasts) is NOT CTL.
  TEST_ASSERT_FALSE(parseControl("lasertag-matrix EVT", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::None, (int)c.kind);
  TEST_ASSERT_FALSE(parseControl("HB id=", c));
  TEST_ASSERT_FALSE(parseControl("random noise 123", c));
  TEST_ASSERT_FALSE(parseControl("", c));
  TEST_ASSERT_FALSE(parseControl("EVT wat foo=bar", c));
  TEST_ASSERT_FALSE(parseControl(nullptr, c));
  // Recognised class, unknown subtype.
  TEST_ASSERT_FALSE(parseControl("CTL wat foo=bar", c));
  // A full heartbeat line is not a control message.
  TEST_ASSERT_FALSE(parseControl(
      "lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 "
      "mode=team-colours hp=100 online=1",
      c));
}

// --- §2.2 Config serialize golden vector -----------------------------------

void test_serialize_config_golden() {
  ConfigDoc cfg;
  strcpy(cfg.deviceId, "a1b2c3");
  strcpy(cfg.hostname, "lasertag-matrix");
  cfg.ownTeam = 2;
  cfg.enabledTeams[0] = 1;
  cfg.enabledTeams[1] = 2;
  cfg.enabledTeams[2] = 3;
  cfg.enabledTeams[3] = 4;
  cfg.enabledTeamsCount = 4;
  strcpy(cfg.protocolId, "vatos");
  cfg.brightness = 13;
  // teamIndex/teamColour defaults are 1..4 -> B/R/G/W.

  char buf[512];
  serializeConfig(cfg, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(
      "{\"deviceId\":\"a1b2c3\",\"hostname\":\"lasertag-matrix\",\"ownTeam\":2,"
      "\"enabledTeams\":[1,2,3,4],\"protocolId\":\"vatos\",\"brightness\":13,"
      "\"teamColours\":{\"1\":\"#0000FF\",\"2\":\"#FF0000\",\"3\":\"#00FF00\","
      "\"4\":\"#FFFFFF\"},\"teamSfx\":{\"1\":0,\"2\":2,\"3\":3,\"4\":5},"
      "\"deathSfx\":6,\"startHp\":32,\"damageMultiplier\":1,"
      "\"teamDamageMult\":{\"1\":0,\"2\":0,\"3\":0,\"4\":0},"
      "\"chaseColour\":\"#FFA500\"}",
      buf);
}

// --- §2.2 PATCH applies provided keys, leaves others unchanged --------------

void test_patch_config_partial() {
  ConfigDoc cfg;
  strcpy(cfg.deviceId, "a1b2c3");
  strcpy(cfg.hostname, "lasertag-matrix");
  cfg.ownTeam = 2;
  cfg.brightness = 13;

  PatchResult r = applyConfigPatch("{\"ownTeam\":3,\"brightness\":20}", cfg);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(3, cfg.ownTeam);
  TEST_ASSERT_EQUAL_INT(20, cfg.brightness);
  // Unchanged fields preserved.
  TEST_ASSERT_EQUAL_STRING("a1b2c3", cfg.deviceId);
  TEST_ASSERT_EQUAL_STRING("lasertag-matrix", cfg.hostname);
}

// --- §2.2 PATCH unknown field rejected, config unchanged -------------------

void test_patch_config_unknown_field_rejected() {
  ConfigDoc cfg;
  cfg.ownTeam = 2;
  cfg.brightness = 13;
  PatchResult r = applyConfigPatch("{\"foo\":1}", cfg);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("unknown field: foo", r.error);
  // cfg untouched.
  TEST_ASSERT_EQUAL_INT(2, cfg.ownTeam);
  TEST_ASSERT_EQUAL_INT(13, cfg.brightness);
}

void test_patch_config_unknown_field_leaves_known_unchanged() {
  // A valid key followed by an unknown one must roll back the valid one too.
  ConfigDoc cfg;
  cfg.ownTeam = 2;
  PatchResult r = applyConfigPatch("{\"ownTeam\":9,\"bar\":2}", cfg);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("unknown field: bar", r.error);
  TEST_ASSERT_EQUAL_INT(2, cfg.ownTeam);
}

void test_patch_config_arrays_and_colours() {
  ConfigDoc cfg;
  cfg.enabledTeamsCount = 0;
  PatchResult r = applyConfigPatch(
      "{\"enabledTeams\":[2,4],\"teamColours\":{\"2\":\"#112233\"}}", cfg);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_size_t(2, cfg.enabledTeamsCount);
  TEST_ASSERT_EQUAL_INT(2, cfg.enabledTeams[0]);
  TEST_ASSERT_EQUAL_INT(4, cfg.enabledTeams[1]);
  // teamIndex[1] is team 2.
  TEST_ASSERT_EQUAL_STRING("#112233", cfg.teamColour[1]);
  // Team 1 colour unchanged.
  TEST_ASSERT_EQUAL_STRING("#0000FF", cfg.teamColour[0]);
}

void test_patch_config_round_trip() {
  ConfigDoc cfg;
  strcpy(cfg.deviceId, "a1b2c3");
  strcpy(cfg.hostname, "lasertag-matrix");
  cfg.ownTeam = 2;
  cfg.enabledTeams[0] = 1;
  cfg.enabledTeams[1] = 2;
  cfg.enabledTeams[2] = 3;
  cfg.enabledTeams[3] = 4;
  cfg.enabledTeamsCount = 4;
  strcpy(cfg.protocolId, "vatos");
  cfg.brightness = 13;

  char buf[512];
  serializeConfig(cfg, buf, sizeof(buf));

  // Feed the serialized config back as a PATCH onto a blank config: should
  // reproduce the same serialization.
  ConfigDoc blank;
  blank.enabledTeamsCount = 0;
  PatchResult r = applyConfigPatch(buf, blank);
  TEST_ASSERT_TRUE(r.ok);
  char buf2[512];
  serializeConfig(blank, buf2, sizeof(buf2));
  TEST_ASSERT_EQUAL_STRING(buf, buf2);
}

void test_patch_config_start_hp_valid_and_invalid() {
  ConfigDoc cfg;
  PatchResult ok = applyConfigPatch("{\"startHp\":8}", cfg);
  TEST_ASSERT_TRUE(ok.ok);
  TEST_ASSERT_EQUAL_INT(8, cfg.startHp);
  // A non-enumerated value is rejected and leaves the config unchanged.
  PatchResult bad = applyConfigPatch("{\"startHp\":10}", cfg);
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_STRING("startHp must be 4/8/16/32", bad.error);
  TEST_ASSERT_EQUAL_INT(8, cfg.startHp);
}

void test_patch_config_damage_multiplier_valid_and_invalid() {
  ConfigDoc cfg;
  TEST_ASSERT_EQUAL_INT(1, cfg.damageMultiplier); // default 1x
  PatchResult ok = applyConfigPatch("{\"damageMultiplier\":8}", cfg);
  TEST_ASSERT_TRUE(ok.ok);
  TEST_ASSERT_EQUAL_INT(8, cfg.damageMultiplier);
  // "custom" = any value in 1..32, not just the 1/2/4/8/16 presets.
  ok = applyConfigPatch("{\"damageMultiplier\":3}", cfg);
  TEST_ASSERT_TRUE(ok.ok);
  TEST_ASSERT_EQUAL_INT(3, cfg.damageMultiplier);
  PatchResult bad = applyConfigPatch("{\"damageMultiplier\":0}", cfg);
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_STRING("damageMultiplier must be 1-32", bad.error);
  TEST_ASSERT_EQUAL_INT(3, cfg.damageMultiplier);
  bad = applyConfigPatch("{\"damageMultiplier\":33}", cfg);
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_INT(3, cfg.damageMultiplier);
}

void test_patch_config_team_damage_mult() {
  ConfigDoc cfg;
  // Keyed by team index like teamSfx; 0 = inherit the global multiplier.
  PatchResult ok = applyConfigPatch("{\"teamDamageMult\":{\"2\":4}}", cfg);
  TEST_ASSERT_TRUE(ok.ok);
  TEST_ASSERT_EQUAL_INT(4, cfg.teamDamageMult[1]); // teamIndex[1] is team 2
  TEST_ASSERT_EQUAL_INT(0, cfg.teamDamageMult[0]); // others still inherit
  PatchResult clear = applyConfigPatch("{\"teamDamageMult\":{\"2\":0}}", cfg);
  TEST_ASSERT_TRUE(clear.ok);
  TEST_ASSERT_EQUAL_INT(0, cfg.teamDamageMult[1]);
  PatchResult bad = applyConfigPatch("{\"teamDamageMult\":{\"2\":33}}", cfg);
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_STRING("teamDamageMult must be 0-32", bad.error);
  TEST_ASSERT_EQUAL_INT(0, cfg.teamDamageMult[1]);
}

// --- §2.2 Status serialize golden vector -----------------------------------

void test_serialize_status_golden() {
  StatusDoc st;
  strcpy(st.deviceId, "a1b2c3");
  strcpy(st.hostname, "lasertag-matrix");
  strcpy(st.fw, "2.0.0");
  strcpy(st.mode, "team-colours");
  st.ownTeam = 2;
  st.hp = 100;
  st.online = true;
  st.uptimeMs = 123456;
  st.rssi = -67;

  char buf[256];
  serializeStatus(st, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(
      "{\"deviceId\":\"a1b2c3\",\"hostname\":\"lasertag-matrix\",\"fw\":\"2.0.0"
      "\",\"mode\":\"team-colours\",\"ownTeam\":2,\"hp\":100,\"online\":true,"
      "\"uptimeMs\":123456,\"rssi\":-67}",
      buf);
}

// --- §2.2 Mode parse + echo golden vector ----------------------------------

void test_parse_and_serialize_mode_golden() {
  ModeDoc m;
  TEST_ASSERT_TRUE(parseMode(
      "{\"mode\":\"team-colours\",\"timings\":{\"darkMinMs\":5000,"
      "\"darkMaxMs\":15000}}",
      m));
  TEST_ASSERT_EQUAL_STRING("team-colours", m.mode);
  TEST_ASSERT_TRUE(m.hasDarkMin);
  TEST_ASSERT_EQUAL_INT(5000, m.darkMinMs);
  TEST_ASSERT_TRUE(m.hasDarkMax);
  TEST_ASSERT_EQUAL_INT(15000, m.darkMaxMs);

  char buf[256];
  serializeMode(m, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(
      "{\"mode\":\"team-colours\",\"timings\":{\"darkMinMs\":5000,\"darkMaxMs\":"
      "15000}}",
      buf);
}

void test_parse_mode_malformed() {
  ModeDoc m;
  TEST_ASSERT_FALSE(parseMode("not json", m));
  TEST_ASSERT_FALSE(parseMode("{\"timings\":{}}", m)); // missing mode
}

// --- §2.2 Command parse golden vectors -------------------------------------

void test_parse_command_identify() {
  CommandDoc c;
  TEST_ASSERT_TRUE(parseCommand("{\"cmd\":\"identify\"}", c));
  TEST_ASSERT_EQUAL_INT((int)CommandKind::Identify, (int)c.kind);
}

void test_parse_command_bright() {
  CommandDoc c;
  TEST_ASSERT_TRUE(parseCommand("{\"cmd\":\"bright\",\"value\":20}", c));
  TEST_ASSERT_EQUAL_INT((int)CommandKind::Bright, (int)c.kind);
  TEST_ASSERT_EQUAL_INT(20, c.value);
}

void test_parse_command_hit() {
  CommandDoc c;
  TEST_ASSERT_TRUE(parseCommand("{\"cmd\":\"hit\",\"team\":2,\"damage\":2}", c));
  TEST_ASSERT_EQUAL_INT((int)CommandKind::Hit, (int)c.kind);
  TEST_ASSERT_EQUAL_INT(2, c.team);
  TEST_ASSERT_EQUAL_INT(2, c.damage);
}

void test_parse_command_fire() {
  CommandDoc c;
  TEST_ASSERT_TRUE(parseCommand("{\"cmd\":\"fire\",\"team\":3,\"damage\":2}", c));
  TEST_ASSERT_EQUAL_INT((int)CommandKind::Fire, (int)c.kind);
  TEST_ASSERT_EQUAL_INT(3, c.team);
  TEST_ASSERT_EQUAL_INT(2, c.damage);
}

void test_parse_command_debug() {
  CommandDoc c;
  TEST_ASSERT_TRUE(parseCommand("{\"cmd\":\"debug\",\"value\":1}", c));
  TEST_ASSERT_EQUAL_INT((int)CommandKind::Debug, (int)c.kind);
  TEST_ASSERT_EQUAL_INT(1, c.value);
}

void test_parse_command_unknown_rejected() {
  CommandDoc c;
  TEST_ASSERT_FALSE(parseCommand("{\"cmd\":\"nope\"}", c));
  TEST_ASSERT_FALSE(parseCommand("{}", c));
  TEST_ASSERT_FALSE(parseCommand("garbage", c));
}

// --- §3 TagEvent adapter ----------------------------------------------------

void test_tag_event_from_vatos_shot() {
  TagEvent ev = tagEventFromVatosShot(2, 3);
  TEST_ASSERT_EQUAL_STRING("vatos", ev.protocolId);
  TEST_ASSERT_EQUAL_INT(2, ev.team);
  TEST_ASSERT_EQUAL_INT(3, ev.damage);
  TEST_ASSERT_EQUAL_INT(-1, ev.playerId);
  TEST_ASSERT_EQUAL_UINT32(0u, ev.rawCode);
  TEST_ASSERT_EQUAL_UINT32(0u, ev.flags);
}

// --- Hit event round-trips through the TagEvent adapter ---------------------

void test_hit_event_from_tag_event() {
  TagEvent ev = tagEventFromVatosShot(2, 2);
  char buf[160];
  formatHitEvent(buf, sizeof(buf), "a1b2c3", ev.team, ev.damage, ev.protocolId,
                 80, 12345);
  TEST_ASSERT_EQUAL_STRING(
      "EVT hit victim=a1b2c3 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345",
      buf);
}

// --- v2.1: id= filter, countdown/gameover/activate/deactivate --------------

void test_parse_control_id_filter_and_new_verbs() {
  Control c;
  // id= captured on any verb.
  TEST_ASSERT_TRUE(parseControl("CTL reset hp=0 id=eb20f8", c));
  TEST_ASSERT_TRUE(c.hasId);
  TEST_ASSERT_EQUAL_STRING("eb20f8", c.id);
  TEST_ASSERT_TRUE(c.hasHp);
  TEST_ASSERT_EQUAL_INT(0, c.hp);
  // No id= -> hasId false (broadcast).
  TEST_ASSERT_TRUE(parseControl("CTL start", c));
  TEST_ASSERT_FALSE(c.hasId);
  // countdown / gameover.
  TEST_ASSERT_TRUE(parseControl("CTL countdown n=5", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Countdown, (int)c.kind);
  TEST_ASSERT_TRUE(c.hasN);
  TEST_ASSERT_EQUAL_INT(5, c.n);
  TEST_ASSERT_TRUE(parseControl("CTL gameover winner=2", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::GameOver, (int)c.kind);
  TEST_ASSERT_TRUE(c.hasWinner);
  TEST_ASSERT_EQUAL_INT(2, c.winner);
  // activate with window + id last.
  TEST_ASSERT_TRUE(parseControl("CTL activate t=3200 id=752b38", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Activate, (int)c.kind);
  TEST_ASSERT_TRUE(c.hasT);
  TEST_ASSERT_EQUAL_UINT32(3200, c.t);
  TEST_ASSERT_EQUAL_STRING("752b38", c.id);
  TEST_ASSERT_TRUE(parseControl("CTL activate", c));
  TEST_ASSERT_FALSE(c.hasT);
  TEST_ASSERT_TRUE(parseControl("CTL deactivate", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Deactivate, (int)c.kind);
}

void test_parse_control_chase_and_score() {
  Control c;
  TEST_ASSERT_TRUE(parseControl("CTL chase on penalty=1 display=dark", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::ChaseOn, (int)c.kind);
  TEST_ASSERT_EQUAL_INT(1, c.penalty);
  TEST_ASSERT_FALSE(c.displayScore);
  TEST_ASSERT_TRUE(parseControl("CTL chase on penalty=0 display=score", c));
  TEST_ASSERT_EQUAL_INT(0, c.penalty);
  TEST_ASSERT_TRUE(c.displayScore);
  // Defaults when keys omitted: penalty 0, display score.
  TEST_ASSERT_TRUE(parseControl("CTL chase on", c));
  TEST_ASSERT_EQUAL_INT(0, c.penalty);
  TEST_ASSERT_TRUE(c.displayScore);
  TEST_ASSERT_TRUE(parseControl("CTL chase off", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::ChaseOff, (int)c.kind);
  // Unknown chase subtype drops.
  TEST_ASSERT_FALSE(parseControl("CTL chase wat", c));
  // score with all four teams.
  TEST_ASSERT_TRUE(parseControl("CTL score 1=4 2=0 3=12 4=7", c));
  TEST_ASSERT_EQUAL_INT((int)ControlKind::Score, (int)c.kind);
  TEST_ASSERT_TRUE(c.hasScores);
  TEST_ASSERT_EQUAL_INT(4, c.scores[0]);
  TEST_ASSERT_EQUAL_INT(0, c.scores[1]);
  TEST_ASSERT_EQUAL_INT(12, c.scores[2]);
  TEST_ASSERT_EQUAL_INT(7, c.scores[3]);
  // Missing team keys default to 0.
  TEST_ASSERT_TRUE(parseControl("CTL score 2=9", c));
  TEST_ASSERT_EQUAL_INT(0, c.scores[0]);
  TEST_ASSERT_EQUAL_INT(9, c.scores[1]);
}

void test_format_dormant_hit_event() {
  char buf[128];
  int n = formatDormantHitEvent(buf, sizeof(buf), "eb20f8", 3, 2, "vatos", 32,
                                1234);
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_EQUAL_STRING(
      "EVT hit victim=eb20f8 shooterTeam=3 dmg=2 proto=vatos hp=32 ts=1234 "
      "dormant=1",
      buf);
}

void test_config_chase_colour_serialize_patch() {
  ConfigDoc cfg;
  strncpy(cfg.deviceId, "a1b2c3", sizeof(cfg.deviceId) - 1);
  strncpy(cfg.hostname, "lasertag-matrix", sizeof(cfg.hostname) - 1);
  cfg.ownTeam = 2;
  cfg.enabledTeams[0] = 1;
  cfg.enabledTeams[1] = 2;
  cfg.enabledTeams[2] = 3;
  cfg.enabledTeams[3] = 4;
  cfg.enabledTeamsCount = 4;
  char buf[640];
  serializeConfig(cfg, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"chaseColour\":\"#FFA500\""));
  PatchResult r = applyConfigPatch("{\"chaseColour\":\"#00FFAA\"}", cfg);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("#00FFAA", cfg.chaseColour);
  r = applyConfigPatch("{\"chaseColour\":\"red\"}", cfg);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("#00FFAA", cfg.chaseColour); // unchanged on reject
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_format_heartbeat_golden);
  RUN_TEST(test_format_hit_event_golden);
  RUN_TEST(test_format_state_dead_omits_hp);
  RUN_TEST(test_format_state_respawn_includes_hp);
  RUN_TEST(test_format_state_ready_and_idle);
  RUN_TEST(test_format_state_stunned_includes_hp);
  RUN_TEST(test_parse_ctl_start_with_ts);
  RUN_TEST(test_parse_ctl_start_without_ts);
  RUN_TEST(test_parse_ctl_stop);
  RUN_TEST(test_parse_ctl_reset_with_hp);
  RUN_TEST(test_parse_ctl_reset_without_hp);
  RUN_TEST(test_parse_garbage_drops);
  RUN_TEST(test_serialize_config_golden);
  RUN_TEST(test_patch_config_partial);
  RUN_TEST(test_patch_config_unknown_field_rejected);
  RUN_TEST(test_patch_config_unknown_field_leaves_known_unchanged);
  RUN_TEST(test_patch_config_arrays_and_colours);
  RUN_TEST(test_patch_config_round_trip);
  RUN_TEST(test_patch_config_start_hp_valid_and_invalid);
  RUN_TEST(test_patch_config_damage_multiplier_valid_and_invalid);
  RUN_TEST(test_patch_config_team_damage_mult);
  RUN_TEST(test_serialize_status_golden);
  RUN_TEST(test_parse_and_serialize_mode_golden);
  RUN_TEST(test_parse_mode_malformed);
  RUN_TEST(test_parse_command_identify);
  RUN_TEST(test_parse_command_bright);
  RUN_TEST(test_parse_command_hit);
  RUN_TEST(test_parse_command_fire);
  RUN_TEST(test_parse_command_debug);
  RUN_TEST(test_parse_command_unknown_rejected);
  RUN_TEST(test_tag_event_from_vatos_shot);
  RUN_TEST(test_hit_event_from_tag_event);
  RUN_TEST(test_parse_control_id_filter_and_new_verbs);
  RUN_TEST(test_parse_control_chase_and_score);
  RUN_TEST(test_format_dormant_hit_event);
  RUN_TEST(test_config_chase_colour_serialize_patch);
  return UNITY_END();
}
