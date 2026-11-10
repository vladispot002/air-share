#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* LandropHandle;

/* Return codes (Negative values indicate errors) */
#define LANDROP_OK                         0
#define LANDROP_ERR_INVALID_ARG           -1
#define LANDROP_ERR_BUFFER_TOO_SMALL      -2
#define LANDROP_ERR_INVALID_HANDLE        -3
#define LANDROP_ERR_INVALID_ROOM_CODE     -4
#define LANDROP_ERR_NETWORK_DISCONNECTED  -5
#define LANDROP_ERR_TIMEOUT               -6
#define LANDROP_ERR_ILLEGAL_STATE         -7
#define LANDROP_ERR_NOT_SUPPORTED         -8
#define LANDROP_ERR_FILE_IO               -9
#define LANDROP_ERR_TRANSFER_CANCELLED    -10
#define LANDROP_ERR_CHECKSUM_MISMATCH     -11
#define LANDROP_ERR_INTERNAL              -12

/* Ephemeral Session States */
typedef enum {
    LANDROP_SESSION_IDLE = 0,
    LANDROP_SESSION_CONNECTING_SIGNALING = 1,
    LANDROP_SESSION_WAITING_FOR_PEER = 2,
    LANDROP_SESSION_GATHERING_CANDIDATES = 3,
    LANDROP_SESSION_NEGOTIATING_SDP = 4,
    LANDROP_SESSION_CONNECTING_TRANSPORT = 5,
    LANDROP_SESSION_CONNECTED = 6,
    LANDROP_SESSION_DISCONNECTED = 7,
    LANDROP_SESSION_FAILED = 8,
    LANDROP_SESSION_CLOSED = 9
} LandropSessionState;

/* File Transfer States */
typedef enum {
    LANDROP_XFER_IDLE = 0,
    LANDROP_XFER_IN_PROGRESS = 1,
    LANDROP_XFER_COMPLETED = 2,
    LANDROP_XFER_FAILED = 3,
    LANDROP_XFER_CANCELLED = 4
} LandropTransferState;

/* QR Error Correction Level */
typedef enum {
    LANDROP_QR_ECC_LOW = 0,      /* ~7% recovery */
    LANDROP_QR_ECC_MEDIUM = 1,   /* ~15% recovery (default) */
    LANDROP_QR_ECC_QUARTILE = 2, /* ~25% recovery */
    LANDROP_QR_ECC_HIGH = 3      /* ~30% recovery */
} LandropQrEccLevel;

/* Real-time Transfer Snapshot for Mobile UI Polling */
typedef struct {
    uint64_t bytes_transferred;
    uint64_t total_bytes;
    double progress_percentage;   /* 0.0 to 100.0 */
    double speed_bytes_per_sec;
    int32_t transfer_state;       /* LandropTransferState */
    char file_name[256];
    char error_message[256];
} LandropTransferSnapshot;

/* ========================================================================= */
/* 1. Context Lifecycle Management                                           */
/* ========================================================================= */
LandropHandle landrop_mobile_create(const char* device_name, const char* device_uuid);
void landrop_mobile_destroy(LandropHandle handle);

/* ========================================================================= */
/* 2. Optical Pairing & QR Code Generation (Zero-Dependency ISO/IEC 18004)    */
/* ========================================================================= */
int landrop_mobile_qr_generate_svg(
    const char* text,
    int ecc_level,
    int border_modules,
    const char* fg_hex,
    const char* bg_hex,
    char* out_buf,
    int buf_cap,
    int* out_required_len
);

int landrop_mobile_qr_generate_matrix(
    const char* text,
    int ecc_level,
    uint8_t* out_matrix,
    int matrix_cap,
    int* out_size
);

int landrop_mobile_qr_generate_pairing_uri(
    const char* room_code,
    const char* host_domain,
    char* out_uri,
    int uri_cap
);

/* ========================================================================= */
/* 3. Crockford Base32 Room Code Normalization                               */
/* ========================================================================= */
int landrop_mobile_normalize_room_code(
    const char* input_code,
    char* out_buf,
    int buf_cap,
    char* out_err,
    int err_cap
);

/* ========================================================================= */
/* 4. Asynchronous WebRTC Ephemeral Session Management                       */
/* ========================================================================= */
int landrop_mobile_webrtc_initiate_room(
    LandropHandle handle,
    const char* signaling_ws_url,
    const char* signaling_http_url
);

int landrop_mobile_webrtc_join_room(
    LandropHandle handle,
    const char* signaling_ws_url,
    const char* signaling_http_url,
    const char* room_code
);

int landrop_mobile_webrtc_get_state(LandropHandle handle);

int landrop_mobile_webrtc_get_room_code(
    LandropHandle handle,
    char* out_buf,
    int buf_cap
);

int landrop_mobile_webrtc_get_peer_id(
    LandropHandle handle,
    char* out_buf,
    int buf_cap
);

void landrop_mobile_webrtc_close(LandropHandle handle);

/* ========================================================================= */
/* 5. Non-Blocking File Streaming over WebRTC DataChannel                    */
/* ========================================================================= */
int landrop_mobile_webrtc_send_file_async(
    LandropHandle handle,
    const char* file_path,
    uint32_t speed_limit_kbps
);

int landrop_mobile_webrtc_receive_file_async(
    LandropHandle handle,
    const char* save_dir
);

int landrop_mobile_webrtc_get_transfer_snapshot(
    LandropHandle handle,
    LandropTransferSnapshot* out_snapshot
);

int landrop_mobile_webrtc_cancel_transfer(LandropHandle handle);

/* ========================================================================= */
/* 6. Local Subnet Discovery & NFC / AirDrop Emulation (Backward Compatible)  */
/* ========================================================================= */
int landrop_mobile_start_discovery(LandropHandle handle, uint16_t port);
void landrop_mobile_stop_discovery(LandropHandle handle);

int landrop_mobile_get_peer_count(LandropHandle handle);
int landrop_mobile_get_peer_info(LandropHandle handle, int index, char* out_name, int name_cap, char* out_ip, int ip_cap, uint16_t* out_port);

int landrop_mobile_send_file(LandropHandle handle, const char* file_path, const char* target_ip, uint16_t target_port);

int landrop_mobile_nfc_generate_payload(LandropHandle handle, const char* local_ip, uint16_t port, char* out_buf, int buf_cap);
int landrop_mobile_nfc_process_payload(LandropHandle handle, const char* raw_payload, char* out_paired_name, int name_cap);

int landrop_mobile_airdrop_enable(LandropHandle handle, int enable);
int landrop_mobile_airdrop_get_state(LandropHandle handle);

#ifdef __cplusplus
}
#endif

