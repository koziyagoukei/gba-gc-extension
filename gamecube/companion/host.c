#include "host.h"
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include "pkmn_protocol.h"

static Frame reply;

bool host_open(HostLink *link) {
    Frame request = {.type = HELLO};
    memset(link, 0, sizeof(*link));
    link->session.timeout_ms = HOST_REPLY_TIMEOUT_MS;
    if (!session_request(&link->session, &request, &reply, NO_FAULT) || reply.len != 0) return false;
    const Caps caps = {MSG_PROTOCOL_VERSION, PEER_GC_HOST, HOST_BUILD_ID,
                       CAP_APP | CAP_ASSETS | CAP_BYE | CAP_MODE | CAP_RESULT | CAP_RLE,
                       WIRE_PAYLOAD, APP_POKEMON, PKMN_APP_VERSION, {'G', 'C', 'X', 'H'}};
    request.type = CAPS;
    request.len = (uint16_t)caps_encode(&caps, request.payload);
    // A Phase 1 PoC client NACKs CAPS; session_request then fails after retries.
    if (!session_request(&link->session, &request, &reply, NO_FAULT)) return false;
    if (!caps_decode(reply.payload, reply.len, &link->client) || link->client.version < MSG_PROTOCOL_MIN_VERSION)
        return false;
    SYS_Report("PH2 CAPS peer=%u build=%08x caps=%x app=%04x v%u max=%u tag=%.4s\n", link->client.peer_type,
               link->client.build_id, link->client.caps, link->client.app_id, link->client.app_version,
               link->client.max_payload, (const char *)link->client.tag);
    return true;
}

bool host_app(HostLink *link, uint16_t opcode, const uint8_t *args, unsigned length,
              const uint8_t **data, unsigned *data_length, uint16_t *status) {
    return host_app_to(link, link->client.app_id, opcode, args, length, data, data_length, status);
}

bool host_app_to(HostLink *link, uint16_t app_id, uint16_t opcode, const uint8_t *args, unsigned length,
                 const uint8_t **data, unsigned *data_length, uint16_t *status) {
    Frame request = {.type = APP};
    if (length > WIRE_PAYLOAD - APP_REQUEST_HEADER) return false;
    msg_put16(request.payload, app_id);
    msg_put16(request.payload + 2, opcode);
    memcpy(request.payload + APP_REQUEST_HEADER, args, length);
    request.len = (uint16_t)(APP_REQUEST_HEADER + length);
    if (!session_request(&link->session, &request, &reply, NO_FAULT) || reply.len < APP_REPLY_HEADER ||
        msg_get16(reply.payload + 2) != opcode)
        return false;
    *status = msg_get16(reply.payload + 4);
    if (data) *data = reply.payload + APP_REPLY_HEADER;
    if (data_length) *data_length = reply.len - APP_REPLY_HEADER;
    return true;
}

bool host_mode(HostLink *link, ModeInfo *mode) {
    Frame request = {.type = MODE};
    if (!(link->client.caps & CAP_MODE)) {
        *mode = (ModeInfo){.mode_id = GC_MODE_PARTY_VIEW, .mode_version = 1};
        return true;
    }
    if (!session_request(&link->session, &request, &reply, NO_FAULT) || !mode_info_decode(reply.payload, reply.len, mode))
        return false;
    SYS_Report("PH3 MODE id=%u v%u app=%04x v%u request=%u crc=%08x nonce=%08x\n", mode->mode_id, mode->mode_version,
               mode->app_id, mode->app_version, mode->request_length, mode->request_crc, mode->nonce);
    return true;
}

bool host_result(HostLink *link, uint16_t mode_id, uint16_t version, uint32_t result_id,
                 const uint8_t *payload, unsigned length, uint16_t *status) {
    Frame request = {.type = RESULT};
    if (!(link->client.caps & CAP_RESULT) || length > RESULT_MAX_PAYLOAD) return false;
    msg_put16(request.payload, mode_id);
    msg_put16(request.payload + 2, version);
    wire_put32(request.payload + 4, result_id);
    memcpy(request.payload + RESULT_HEADER_BYTES, payload, length);
    request.len = (uint16_t)(RESULT_HEADER_BYTES + length);
    if (!session_request(&link->session, &request, &reply, NO_FAULT) || reply.len < 2) return false;
    *status = msg_get16(reply.payload);
    return true;
}

bool host_bye(HostLink *link, uint16_t reason) {
    Frame request = {.type = BYE, .len = 2};
    msg_put16(request.payload, reason);
    return session_request(&link->session, &request, &reply, NO_FAULT);
}

void fetch_start(AssetFetch *fetch, const AssetKey *key) {
    fetch->key = *key;
    fetch->offset = 0;
    fetch->crc = UINT32_MAX;
    fetch->active = true;
    fetch->have_info = false;
    fetch->started = gettime();
}

uint32_t fetch_elapsed_us(const AssetFetch *fetch) {
    return (uint32_t)ticks_to_microsecs(gettime() - fetch->started);
}

enum FetchResult fetch_step(HostLink *link, AssetFetch *fetch) {
    Frame request;
    if (!fetch->active) return FETCH_FAILED;
    if (!fetch->have_info) {
        request.type = ASSET_INFO;
        request.len = (uint16_t)asset_key_encode(&fetch->key, request.payload);
        if (!session_request(&link->session, &request, &reply, NO_FAULT) ||
            !asset_info_decode(reply.payload, reply.len, &fetch->info)) {
            fetch->active = false;
            return FETCH_FAILED;
        }
        if (fetch->info.status != ASSET_OK || fetch->info.length == 0 || fetch->info.length > ASSET_STAGING_BYTES ||
            fetch->info.raw_length == 0 || fetch->info.raw_length > ASSET_STAGING_BYTES ||
            fetch->info.encoding > ASSET_ENCODING_RLE ||
            !asset_key_equal(&fetch->info.key, &fetch->key)) {
            fetch->active = false;
            return FETCH_MISSING;
        }
        fetch->have_info = true;
        fetch->wire_length = fetch->info.length;
        return FETCH_PENDING;
    }
    const uint32_t remaining = fetch->info.length - fetch->offset;
    const uint32_t length = remaining > WIRE_PAYLOAD ? WIRE_PAYLOAD : remaining;
    request.type = ASSET_CHUNK;
    request.len = ASSET_CHUNK_REQUEST_BYTES;
    asset_key_encode(&fetch->key, request.payload);
    wire_put32(request.payload + ASSET_KEY_BYTES, fetch->offset);
    wire_put32(request.payload + ASSET_KEY_BYTES + 4, length);
    if (!session_request(&link->session, &request, &reply, NO_FAULT) || reply.len != length) {
        fetch->active = false;
        return FETCH_FAILED;
    }
    memcpy(fetch->wire + fetch->offset, reply.payload, length);
    fetch->crc = wire_crc(fetch->crc, reply.payload, length);
    fetch->offset += length;
    if (fetch->offset < fetch->info.length) return FETCH_PENDING;
    fetch->active = false;
    if (~fetch->crc != fetch->info.crc) {
        SYS_Report("PH2 ASSET CRC MISMATCH type=%04x id=%08x got=%08x want=%08x\n", fetch->key.type,
                   fetch->key.id, ~fetch->crc, fetch->info.crc);
        return FETCH_FAILED;
    }
    if (fetch->info.encoding == ASSET_ENCODING_RLE) {
        if (rle_decode(fetch->wire, fetch->wire_length, fetch->data, ASSET_STAGING_BYTES) != fetch->info.raw_length) {
            SYS_Report("PH4 ASSET RLE decode failed type=%04x id=%08x\n", fetch->key.type, fetch->key.id);
            return FETCH_FAILED;
        }
    } else {
        memcpy(fetch->data, fetch->wire, fetch->wire_length);
    }
    fetch->info.length = fetch->info.raw_length;  // callers see decoded data
    return FETCH_DONE;
}
