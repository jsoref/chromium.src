// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/content_hash_reader.h"

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/timer/elapsed_timer.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "extensions/browser/computed_hashes.h"
#include "extensions/browser/content_hash_tree.h"
#include "extensions/browser/verified_contents.h"
#include "extensions/common/extension.h"
#include "extensions/common/file_util.h"

using base::DictionaryValue;
using base::ListValue;
using base::Value;

namespace extensions {

ContentHashReader::ContentHashReader(const std::string& extension_id,
                                     const base::Version& extension_version,
                                     const base::FilePath& extension_root,
                                     const base::FilePath& relative_path,
                                     const ContentVerifierKey& key)
    : extension_id_(extension_id),
      extension_version_(extension_version.GetString()),
      extension_root_(extension_root),
      relative_path_(relative_path),
      key_(key),
      status_(NOT_INITIALIZED),
      have_verified_contents_(false),
      have_computed_hashes_(false),
      file_missing_from_verified_contents_(false),
      block_size_(0) {}

ContentHashReader::~ContentHashReader() {
}

bool ContentHashReader::Init() {
  base::ElapsedTimer timer;
  DCHECK_EQ(status_, NOT_INITIALIZED);
  status_ = FAILURE;
  base::FilePath verified_contents_path =
      file_util::GetVerifiedContentsPath(extension_root_);
  if (!base::PathExists(verified_contents_path))
    return false;

  VerifiedContents verified_contents(key_.data, key_.size);
  if (!verified_contents.InitFrom(verified_contents_path) ||
      !verified_contents.valid_signature()) {
      //verified_contents.version() != extension_version_ ||
      //verified_contents.extension_id() != extension_id_) {
    return false;
  }

  have_verified_contents_ = true;

  base::FilePath computed_hashes_path =
      file_util::GetComputedHashesPath(extension_root_);
  if (!base::PathExists(computed_hashes_path))
    return false;

  ComputedHashes::Reader reader;
  if (!reader.InitFromFile(computed_hashes_path))
    return false;

  have_computed_hashes_ = true;

  if (!verified_contents.HasTreeHashRoot(relative_path_)) {
    // Extension is requesting a non-existent resource that does not have an
    // entry in verified_contents.json. This can happen when an extension sends
    // XHR to its non-existent resource. This should not result in content
    // verification failure.
    file_missing_from_verified_contents_ = true;
    return false;
  }

  if (!reader.GetHashes(relative_path_, &block_size_, &hashes_) ||
      block_size_ % crypto::kSHA256Length != 0)
    return false;

  std::string root =
      ComputeTreeHashRoot(hashes_, block_size_ / crypto::kSHA256Length);
  if (!verified_contents.TreeHashRootEquals(relative_path_, root))
    return false;

  status_ = SUCCESS;
  UMA_HISTOGRAM_TIMES("ExtensionContentHashReader.InitLatency",
                      timer.Elapsed());
  return true;
}

int ContentHashReader::block_count() const {
  DCHECK(status_ != NOT_INITIALIZED);
  return hashes_.size();
}

int ContentHashReader::block_size() const {
  DCHECK(status_ != NOT_INITIALIZED);
  return block_size_;
}

bool ContentHashReader::GetHashForBlock(int block_index,
                                        const std::string** result) const {
  if (status_ != SUCCESS)
    return false;
  DCHECK(block_index >= 0);

  if (static_cast<unsigned>(block_index) >= hashes_.size())
    return false;
  *result = &hashes_[block_index];

  return true;
}

}  // namespace extensions
