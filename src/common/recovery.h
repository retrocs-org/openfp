/* SPDX-License-Identifier: MIT */
#ifndef OFP_RECOVERY_H
#define OFP_RECOVERY_H
#include <stddef.h>
/* Three independent, checksummed drafts. Prefix belongs to the editor, never
 * a user-selected map. A damaged newest draft falls back to an older one. */
int recovery_read(const char *prefix, char *text, size_t cap);
int recovery_write(const char *prefix, const char *text);
void recovery_clear(const char *prefix);
#endif
