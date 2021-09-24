// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

// This is an implementation of the DiceGenerateCertificate that generates an
// X.509 certificate based on a template using the ED25519-SHA512 signature
// scheme.
//
// If no variable length descriptors are used in a DICE certificate, the
// certificate can be constructed from a template instead of using an ASN.1
// library. This implementation includes only hashes and inline configuration in
// the DICE extension. For convenience this uses the lower level curve25519
// implementation in boringssl but does not use anything else (no ASN.1, X.509,
// etc). This approach may be especially useful in very low level components
// where simplicity is paramount.
//
// This function will return kDiceResultInvalidInput if 'input_values' specifies
// any variable length descriptors. In particular:
//   * code_descriptor_size must be zero
//   * authority_descriptor_size must be zero
//   * config_type must be kDiceConfigTypeInline

#include <stdint.h>
#include <string.h>

#include "dice/dice.h"
#include "dice/ops.h"
#include "dice/utils.h"
#include "openssl/curve25519.h"
#include "openssl/is_boringssl.h"

// A well-formed certificate, but with zeros in all fields to be filled.
static const uint8_t kTemplate[635] = {
    // Constant encoding.
    0x30, 0x82, 0x02, 0x77,
    // Offset 4: TBS starts here.
    // Constant encoding.
    0x30, 0x82, 0x02, 0x29, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x14,
    // Offset 15: Serial number, 20 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x30, 0x33, 0x31, 0x31, 0x30,
    0x2f, 0x06, 0x03, 0x55, 0x04, 0x05, 0x13, 0x28,
    // Offset 55: Issuer name, 40 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0x30, 0x20, 0x17, 0x0d, 0x31, 0x38, 0x30, 0x33, 0x32, 0x32, 0x32, 0x33,
    0x35, 0x39, 0x35, 0x39, 0x5a, 0x18, 0x0f, 0x39, 0x39, 0x39, 0x39, 0x31,
    0x32, 0x33, 0x31, 0x32, 0x33, 0x35, 0x39, 0x35, 0x39, 0x5a, 0x30, 0x33,
    0x31, 0x31, 0x30, 0x2f, 0x06, 0x03, 0x55, 0x04, 0x05, 0x13, 0x28,
    // Offset 142: Subject name, 40 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
    // Offset 194: Subject public key, 32 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0xa3, 0x82, 0x01, 0x4b, 0x30, 0x82, 0x01, 0x47, 0x30, 0x1f, 0x06, 0x03,
    0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14,
    // Offset 247: Authority key identifier, 20 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14,
    // Offset 278: Subject key identifier, 20 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01, 0x01, 0xff, 0x04, 0x04,
    0x03, 0x02, 0x02, 0x04, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01,
    0x01, 0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0x01, 0x30, 0x81, 0xe3,
    0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x18,
    0x04, 0x81, 0xd4, 0x30, 0x81, 0xd1, 0xa0, 0x42, 0x04, 0x40,
    // Offset 356: Code hash, 64 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0xa3, 0x42, 0x04, 0x40,
    // Offset 424: Configuration value, 64 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0xa4, 0x42, 0x04, 0x40,
    // Offset 492: Authority hash, 64 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // Constant encoding.
    0xa6, 0x03, 0x0a, 0x01,
    // Offset 560: Mode, 1 byte.
    0x00,
    // Offset 561: TBS ends here.
    // Constant encoding.
    0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x41, 0x00,
    // Offset 571: Signature, 64 bytes.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

static const struct {
  size_t offset;
  size_t length;
} kFieldTable[] = {{15, 20},   // Serial number
                   {55, 40},   // Issuer name
                   {142, 40},  // Subject name
                   {194, 32},  // Subject public key
                   {247, 20},  // Authority key id
                   {278, 20},  // Subject key id
                   {356, 64},  // Code hash
                   {424, 64},  // Config descriptor
                   {492, 64},  // Authority hash
                   {560, 1},   // Mode
                   {571, 64},  // Signature
                   {4, 557}};  // Entire TBS

static const size_t kFieldIndexSerial = 0;
static const size_t kFieldIndexIssuerName = 1;
static const size_t kFieldIndexSubjectName = 2;
static const size_t kFieldIndexSubjectPublicKey = 3;
static const size_t kFieldIndexAuthorityKeyId = 4;
static const size_t kFieldIndexSubjectKeyId = 5;
static const size_t kFieldIndexCodeHash = 6;
static const size_t kFieldIndexConfigDescriptor = 7;
static const size_t kFieldIndexAuthorityHash = 8;
static const size_t kFieldIndexMode = 9;
static const size_t kFieldIndexSignature = 10;
static const size_t kFieldIndexTbs = 11;

// |buffer| must point to the beginning of the template buffer and |src| must
// point to at least <field-length> bytes.
static void CopyField(const uint8_t* src, size_t index, uint8_t* buffer) {
  memcpy(&buffer[kFieldTable[index].offset], src, kFieldTable[index].length);
}

DiceResult DiceGenerateCertificate(
    void* context,
    const uint8_t subject_private_key_seed[DICE_PRIVATE_KEY_SEED_SIZE],
    const uint8_t authority_private_key_seed[DICE_PRIVATE_KEY_SEED_SIZE],
    const DiceInputValues* input_values, size_t certificate_buffer_size,
    uint8_t* certificate, size_t* certificate_actual_size) {
  DiceResult result = kDiceResultOk;

  // Variable length descriptors are not supported.
  if (input_values->code_descriptor_size > 0 ||
      input_values->config_type != kDiceConfigTypeInline ||
      input_values->authority_descriptor_size > 0) {
    return kDiceResultInvalidInput;
  }

  // We know the certificate size upfront so we can do the buffer size check.
  *certificate_actual_size = sizeof(kTemplate);
  if (certificate_buffer_size < sizeof(kTemplate)) {
    return kDiceResultBufferTooSmall;
  }

  // Declare buffers which are cleared on 'goto out'.
  uint8_t subject_bssl_private_key[64];
  uint8_t authority_bssl_private_key[64];

  // Derive keys and IDs from the private key seeds.
  uint8_t subject_public_key[32];
  ED25519_keypair_from_seed(subject_public_key, subject_bssl_private_key,
                            subject_private_key_seed);

  uint8_t subject_id[20];
  result =
      DiceDeriveCdiCertificateId(context, subject_public_key, 32, subject_id);
  if (result != kDiceResultOk) {
    goto out;
  }
  uint8_t subject_id_hex[40];
  DiceHexEncode(subject_id, sizeof(subject_id), subject_id_hex,
                sizeof(subject_id_hex));

  uint8_t authority_public_key[32];
  ED25519_keypair_from_seed(authority_public_key, authority_bssl_private_key,
                            authority_private_key_seed);

  uint8_t authority_id[20];
  result = DiceDeriveCdiCertificateId(context, authority_public_key, 32,
                                      authority_id);
  if (result != kDiceResultOk) {
    goto out;
  }
  uint8_t authority_id_hex[40];
  DiceHexEncode(authority_id, sizeof(authority_id), authority_id_hex,
                sizeof(authority_id_hex));

  // First copy in the entire template, then fill in the fields.
  memcpy(certificate, kTemplate, sizeof(kTemplate));
  CopyField(subject_id, kFieldIndexSerial, certificate);
  CopyField(authority_id_hex, kFieldIndexIssuerName, certificate);
  CopyField(subject_id_hex, kFieldIndexSubjectName, certificate);
  CopyField(subject_public_key, kFieldIndexSubjectPublicKey, certificate);
  CopyField(authority_id, kFieldIndexAuthorityKeyId, certificate);
  CopyField(subject_id, kFieldIndexSubjectKeyId, certificate);
  CopyField(input_values->code_hash, kFieldIndexCodeHash, certificate);
  CopyField(input_values->config_value, kFieldIndexConfigDescriptor,
            certificate);
  CopyField(input_values->authority_hash, kFieldIndexAuthorityHash,
            certificate);
  certificate[kFieldTable[kFieldIndexMode].offset] = input_values->mode;

  // All the TBS fields are filled in, we're ready to sign.
  uint8_t signature[64];
  if (1 != ED25519_sign(signature,
                        &certificate[kFieldTable[kFieldIndexTbs].offset],
                        kFieldTable[kFieldIndexTbs].length,
                        authority_bssl_private_key)) {
    result = kDiceResultPlatformError;
    goto out;
  }
  if (1 != ED25519_verify(&certificate[kFieldTable[kFieldIndexTbs].offset],
                          kFieldTable[kFieldIndexTbs].length, signature,
                          authority_public_key)) {
    result = kDiceResultPlatformError;
    goto out;
  }
  CopyField(signature, kFieldIndexSignature, certificate);

out:
  DiceClearMemory(context, sizeof(subject_bssl_private_key),
                  subject_bssl_private_key);
  DiceClearMemory(context, sizeof(authority_bssl_private_key),
                  authority_bssl_private_key);
  return result;
}
