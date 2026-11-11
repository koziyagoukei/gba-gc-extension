#include "message.h"
#include "pkmn_protocol.h"
#include <assert.h>
#include <string.h>

static void test_caps(void) {
    const Caps in = {MSG_PROTOCOL_VERSION, PEER_POKEEMERALD_EXPANSION, 0x89abcdefu, CAP_APP | CAP_ASSETS | CAP_BYE,
                     WIRE_PAYLOAD, APP_POKEMON, PKMN_APP_VERSION, {'B','P','E','E'}};
    uint8_t bytes[CAPS_BYTES];
    Caps out;
    assert(caps_encode(&in, bytes) == CAPS_BYTES);
    assert(bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 2 && bytes[4] == 0xef && bytes[7] == 0x89);
    assert(!caps_decode(bytes, CAPS_BYTES - 1, &out));
    assert(caps_decode(bytes, CAPS_BYTES, &out));
    assert(memcmp(&in.tag, &out.tag, 4) == 0 && out.build_id == in.build_id && out.caps == in.caps &&
           out.app_id == APP_POKEMON && out.max_payload == WIRE_PAYLOAD && out.peer_type == in.peer_type);
}

static void test_asset_info(void) {
    const AssetInfo in = {ASSET_OK, {ASSET_PKMN_FRONT, 25, PKMN_SPRITE_SHINY, 0x12345678u}, 64, 64,
                          PIXEL_4BPP_TILED, PALETTE_BGR555_16, 1, 2080, 0xdeadbeefu};
    uint8_t bytes[ASSET_INFO_BYTES];
    AssetInfo out;
    assert(asset_info_encode(&in, bytes) == ASSET_INFO_BYTES);
    assert(asset_info_decode(bytes, sizeof(bytes), &out));
    assert(asset_key_equal(&in.key, &out.key) && out.length == 2080 && out.crc == in.crc &&
           out.width == 64 && out.pixel_format == PIXEL_4BPP_TILED && out.palette_format == PALETTE_BGR555_16);
    assert(asset_palette_bytes(PALETTE_BGR555_16) == 32 && asset_palette_bytes(PALETTE_NONE) == 0);
    assert(ASSET_INFO_BYTES <= WIRE_PAYLOAD - 0 && ASSET_CHUNK_REQUEST_BYTES <= WIRE_PAYLOAD);
}

static void test_record(void) {
    PokemonRecord in = {0}, out;
    in.slot = 3;
    in.personality = 0xa1b2c3d4u;
    in.ot_id = 0x01020304u;
    in.species = 1234;
    in.national_dex = 1025;
    in.form = 2;
    in.level = 100;
    in.nature = 24;
    in.tera_type = 18;
    in.gender = 1;
    in.flags = PKMN_FLAG_SHINY;
    in.ability_slot = 2;
    in.friendship = 255;
    in.hp = 1;
    in.max_hp = 714;
    in.attack = 500; in.defense = 501; in.speed = 502; in.sp_attack = 503; in.sp_defense = 504;
    in.ability = 310;
    in.held_item = 999;
    in.status = 0x80000001u;
    for (unsigned i = 0; i < 4; ++i) { in.moves[i] = (uint16_t)(900 + i); in.pp[i] = (uint8_t)i; in.max_pp[i] = (uint8_t)(40 + i); }
    in.language = 1;
    in.encoding = PKMN_ENCODING_POKEEMERALD;
    in.nickname_length = 5;
    memset(in.nickname, 0xff, sizeof(in.nickname));
    memcpy(in.nickname, "\x51\x52\x53\x54\x55", 5);
    in.sprite = (AssetKey){ASSET_PKMN_FRONT, 1234, PKMN_SPRITE_SHINY | PKMN_SPRITE_FEMALE, 0};
    uint8_t bytes[PKMN_RECORD_BYTES];
    assert(pkmn_record_encode(&in, bytes) == PKMN_RECORD_BYTES);
    assert(PKMN_RECORD_BYTES <= APP_MAX_REPLY_DATA);
    assert(bytes[0] == 1 && bytes[2] == 100 && bytes[12] == (1234 & 255) && bytes[26] == (714 & 255) && bytes[73] == 0xff);
    assert(pkmn_record_decode(bytes, sizeof(bytes), &out));
    assert(out.slot == 3 && out.personality == in.personality && out.species == 1234 && out.level == 100);
    assert(out.hp == 1 && out.max_hp == 714 && out.sp_defense == 504 && out.ability == 310 && out.held_item == 999);
    assert(out.status == in.status && out.moves[3] == 903 && out.pp[2] == 2 && out.max_pp[1] == 41);
    assert(memcmp(out.nickname, in.nickname, PKMN_NICKNAME_BYTES) == 0 && out.nickname_length == 5);
    assert(asset_key_equal(&out.sprite, &in.sprite));
    const AssetKey a = pkmn_nickname_label(&in), b = pkmn_nickname_label(&out);
    assert(asset_key_equal(&a, &b) && LABEL_KIND(a.id) == LABEL_NICKNAME && LABEL_VALUE(a.id) == 3);
    bytes[0] = 2;
    assert(!pkmn_record_decode(bytes, sizeof(bytes), &out));
    assert(!pkmn_record_decode(bytes, PKMN_RECORD_BYTES - 1, &out));
}

static void test_ui(void) {
    const PartyUi in = {6, PKMN_RECORD_VERSION, PKMN_RECORD_BYTES, 4, PKMN_NO_SLOT, SCREEN_PARTY, UI_EXIT_REQUESTED, 0x1234};
    uint8_t bytes[8];
    PartyUi out = {0};
    assert(party_ui_encode(&in, true, bytes) == PARTY_INFO_BYTES);
    assert(party_ui_decode(bytes, 8, true, &out) && out.count == 6 && out.record_length == 100 && out.cursor == 4);
    assert(party_ui_encode(&in, false, bytes) == UI_STATE_BYTES);
    assert(party_ui_decode(bytes, 8, false, &out) && out.input_seq == 0x1234 && out.confirmed == PKMN_NO_SLOT &&
           out.screen == SCREEN_PARTY && out.flags == UI_EXIT_REQUESTED);
}

static void test_new_frame_types(void) {
    Frame in = {.seq = 9, .len = 4, .type = BYE}, out;
    Parser parser = {0};
    uint32_t words[WIRE_WORDS];
    const unsigned n = wire_encode(&in, words);
    assert(n == 5);
    for (unsigned i = 0; i < n; ++i)
        assert(wire_feed(&parser, words[i], &out) == (i+1 == n ? FRAME_OK : INCOMPLETE));
    assert(out.type == BYE);
    in.type = WIRE_TYPE_LAST + 1;
    assert(wire_encode(&in, words) == 0);
}

int main(void) {
    test_caps();
    test_asset_info();
    test_record();
    test_ui();
    test_new_frame_types();
    return 0;
}
