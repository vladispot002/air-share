#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* LandropHandle;

LandropHandle landrop_mobile_create(const char* device_name, const char* device_uuid);
void landrop_mobile_destroy(LandropHandle handle);

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
