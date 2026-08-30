/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <psa/crypto.h>
#include <pouch/types.h>

/** Create /lfs1/credentials if LittleFS is mounted. Safe to call at boot. */
int credentials_prepare(void);

psa_key_id_t load_private_key(void);
int load_certificate(struct pouch_cert *cert);
void free_certificate(struct pouch_cert *cert);
