/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "GCSUtil.h"

#include <string>

#include <folly/Format.h>
#include <folly/Uri.h>

#include <boost/algorithm/string.hpp>
#include "fbpcf/exception/GcpException.h"

namespace fbpcf::gcp {
// Format:
// 1. https://storage.cloud.google.com/bucket-name/key-name
// 2. gs://bucket-name/key-name
GCSObjectReference uriToObjectReference(std::string url) {
  std::string bucket;
  std::string key;
  size_t pos = 0;
  auto uri = folly::Uri(url);
  auto scheme = uri.scheme();
  auto host = uri.host();
  auto path = uri.path();

  if (path.length() <= 1) {
    throw GcpException{folly::sformat(
        "Incorrect GCS URI format: {}"
        "key not specified",
        url)};
  }

  if (boost::iequals(scheme, "gs")) {
    bucket = host;
  } else {
    // Remove the first character '/' in path
    path = path.substr(1);
    pos = path.find("/");
    if (pos == std::string::npos || path.substr(pos + 1).length() == 0) {
      throw GcpException{folly::sformat(
          "Incorrect GCS URI format: {}"
          "bucket/key not specified",
          url)};
    }
    bucket = path.substr(0, pos);
  }

  // path.substr(pos+1) to remove the first character '/'
  return GCSObjectReference{bucket, path.substr(pos + 1)};
}

} // namespace fbpcf::gcp
