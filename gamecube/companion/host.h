#ifndef COMPANION_HOST_H
#define COMPANION_HOST_H
#include "message.h"
#include "../session.h"

// Generic message-layer host: session open (HELLO + CAPS), APP requests,
// chunked asset fetch and BYE. Game protocols build on these calls.

#define HOST_BUILD_ID 0x48320001u  // "H2" companion host, revision 1
#define ASSET_STAGING_BYTES 8192u

typedef struct {
    Session session;
    Caps client;
} HostLink;

bool host_open(HostLink *link);  // after the raw JoyBus handshake
bool host_app(HostLink *link, uint16_t opcode, const uint8_t *args, unsigned length,
              const uint8_t **data, unsigned *data_length, uint16_t *status);
bool host_bye(HostLink *link, uint16_t reason);
// MODE (message layer v3). Clients without CAP_MODE run GC_MODE_PARTY_VIEW.
bool host_mode(HostLink *link, ModeInfo *mode);
// Sends a RESULT for the current mode; *status receives the client's ResultStatus.
bool host_result(HostLink *link, uint16_t mode_id, uint16_t version, uint32_t result_id,
                 const uint8_t *payload, unsigned length, uint16_t *status);
// APP request to a specific app id (the mode app).
bool host_app_to(HostLink *link, uint16_t app_id, uint16_t opcode, const uint8_t *args, unsigned length,
                 const uint8_t **data, unsigned *data_length, uint16_t *status);

enum FetchResult { FETCH_PENDING, FETCH_DONE, FETCH_MISSING, FETCH_FAILED };
typedef struct {
    AssetKey key;
    AssetInfo info;
    uint32_t offset, crc;
    uint64_t started;
    bool active, have_info;
    uint8_t data[ASSET_STAGING_BYTES] __attribute__((aligned(32)));
} AssetFetch;

void fetch_start(AssetFetch *fetch, const AssetKey *key);
// Performs one request (ASSET_INFO or one ASSET_CHUNK). Call once per frame.
enum FetchResult fetch_step(HostLink *link, AssetFetch *fetch);
uint32_t fetch_elapsed_us(const AssetFetch *fetch);
#endif
