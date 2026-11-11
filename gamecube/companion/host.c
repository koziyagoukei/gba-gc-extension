#include "host.h"
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include "pkmn_protocol.h"

static Frame reply;

bool host_open(HostLink *link) {
    Frame request = {.type = HELLO};
    memset(link, 0, sizeof(*link));
    if (!session_request(&link->session, &request, &reply, NO_FAULT) || reply.len != 0) return false;
    const Caps caps = {MSG_PROTOCOL_VERSION, PEER_GC_HOST, HOST_BUILD_ID, CAP_APP | CAP_ASSETS | CAP_BYE,
                       WIRE_PAYLOAD, APP_POKEMON, PKMN_APP_VERSION, {'G', 'C', 'X', 'H'}};
    request.type = CAPS;
    request.len = (uint16_t)caps_encode(&caps, request.payload);
    // A Phase 1 PoC client NACKs CAPS; session_request then fails after retries.
    if (!session_request(&link->session, &request, &reply, NO_FAULT)) return false;
    if (!caps_decode(reply.payload, reply.len, &link->client) || link->client.version < MSG_PROTOCOL_VERSION)
        return false;
    SYS_Report("PH2 CAPS peer=%u build=%08x caps=%x app=%04x v%u max=%u tag=%.4s\n", link->client.peer_type,
               link->client.build_id, link->client.caps, link->client.app_id, link->client.app_version,
               link->client.max_payload, (const char *)link->client.tag);
    return true;
}

bool host_app(HostLink *link, uint16_t opcode, const uint8_t *args, unsigned length,
              const uint8_t **data, unsigned *data_length, uint16_t *status) {
    Frame request = {.type = APP};
    if (length > WIRE_PAYLOAD - APP_REQUEST_HEADER) return false;
    msg_put16(request.payload, link->client.app_id);
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
            !asset_key_equal(&fetch->info.key, &fetch->key)) {
            fetch->active = false;
            return FETCH_MISSING;
        }
        fetch->have_info = true;
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
    memcpy(fetch->data + fetch->offset, reply.payload, length);
    fetch->crc = wire_crc(fetch->crc, reply.payload, length);
    fetch->offset += length;
    if (fetch->offset < fetch->info.length) return FETCH_PENDING;
    fetch->active = false;
    if (~fetch->crc != fetch->info.crc) {
        SYS_Report("PH2 ASSET CRC MISMATCH type=%04x id=%08x got=%08x want=%08x\n", fetch->key.type,
                   fetch->key.id, ~fetch->crc, fetch->info.crc);
        return FETCH_FAILED;
    }
    return FETCH_DONE;
}
