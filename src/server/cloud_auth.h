/* SPDX-License-Identifier: MIT */
#ifndef OFP_SERVER_CLOUD_AUTH_H
#define OFP_SERVER_CLOUD_AUTH_H
#include "common/net.h"
int server_cloud_start(const char *server, const char *secret);
void server_cloud_stop(void);
int server_cloud_verify(void *ctx,const char *ticket,const uint8_t server_id[16],uint64_t nonce,const uint8_t key_id[16],char account[33],char name[NET_NAME]);
#endif
