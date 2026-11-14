/* SPDX-License-Identifier: MIT */
#ifndef OFP_CLOUD_H
#define OFP_CLOUD_H
#include <stddef.h>
#include <stdint.h>
#define CLOUD_API "https://openfp.retrocs.org/account/api/"
#if defined(__amigaos__)
#define CLOUD_CLIENT_TYPE "amigaos"
#elif defined(OFP_TVOS)
#define CLOUD_CLIENT_TYPE "tvos"
#elif defined(_WIN32)
#define CLOUD_CLIENT_TYPE "windows"
#elif defined(__APPLE__)
#define CLOUD_CLIENT_TYPE "macos"
#else
#define CLOUD_CLIENT_TYPE "linux"
#endif
#define CLOUD_PIN_URL "https://openfp.retrocs.org/account/pin"
typedef struct { int tank, avatar, steer; } CloudPreferences;
typedef struct { char name[16], session[65]; uint8_t key[32]; int has_preferences, has_steering; CloudPreferences preferences; } CloudAccount;
typedef struct { char secret[65], pin[9]; } CloudDevice;
/* Passwords are used for this HTTPS call only. 0 success, -1 error. */
int cloud_login(CloudAccount *account, const char *username, const char *email, const char *password, int create, char error[96]);
int cloud_device_start(CloudDevice *device, char error[96]);
/* 1 approved, 0 pending, -1 error. */
int cloud_device_poll(const CloudDevice *device, CloudAccount *account, char error[96]);
int cloud_profile_load(const char *session, CloudAccount *account, char error[96]);
int cloud_profile_save(const char *session, const CloudPreferences *preferences, char error[96]);
void cloud_password_mask(const char *password, char *out, size_t cap);
void cloud_register(const uint8_t key[32], const char *session);
void cloud_forget(const uint8_t key[32]);
/* Called only for servers requiring cloud authentication. No passwords reach UDP. */
int cloud_join_ticket(const uint8_t key[32], const char *server, const uint8_t server_id[16], uint64_t nonce, const uint8_t key_id[16], char ticket[65], char error[96]);
void cloud_hex(const void *data, size_t n, char *out);
int cloud_unhex(const char *hex, void *out, size_t n);
/* Strict line response reader shared with the server verifier. */
int cloud_field(const char *text, const char *name, char *out, size_t cap);
#endif
