// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_EXTRAS_SQLITE_COOKIE_CRYPTO_DELEGATE_H_
#define NET_EXTRAS_SQLITE_COOKIE_CRYPTO_DELEGATE_H_

#include "base/component_export.h"

namespace net {

// Implements encryption and decryption for the persistent cookie store.
class COMPONENT_EXPORT(NET_EXTRAS) CookieCryptoDelegate {
 public:
  CookieCryptoDelegate() = default;

  virtual ~CookieCryptoDelegate() = default;

  CookieCryptoDelegate(const CookieCryptoDelegate&) = delete;
  CookieCryptoDelegate& operator=(const CookieCryptoDelegate&) = delete;

  // Return if cookies should be encrypted on this platform.  Decryption of
  // previously encrypted cookies is always possible.
  virtual bool ShouldEncrypt() = 0;

  // Encrypt `plaintext` string and store the result in `ciphertext`. Returns
  // true if the encryption succeeded. This must only be called after the
  // callback from `Init` has run. This method can be called on any sequence.
  virtual bool EncryptString(const std::string& plaintext,
                             std::string* ciphertext) = 0;

  // Decrypt `ciphertext` string and store the result in `plaintext`. Returns
  // true if the decryption succeeded. This must only be called after the
  // callback from `Init` has run. This method can be called on any sequence.
  virtual bool DecryptString(const std::string& ciphertext,
                             std::string* plaintext) = 0;
};

}  // namespace net

#endif  // NET_EXTRAS_SQLITE_COOKIE_CRYPTO_DELEGATE_H_
