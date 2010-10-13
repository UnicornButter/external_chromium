// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_http_utils.h"

#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/time.h"
#include "net/base/load_flags.h"
#include "net/base/net_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_util.h"

namespace net {

bool SpdyHeadersToHttpResponse(const spdy::SpdyHeaderBlock& headers,
                               HttpResponseInfo* response) {
  std::string version;
  std::string status;

  // The "status" and "version" headers are required.
  spdy::SpdyHeaderBlock::const_iterator it;
  it = headers.find("status");
  if (it == headers.end()) {
    LOG(ERROR) << "SpdyHeaderBlock without status header.";
    return false;
  }
  status = it->second;

  // Grab the version.  If not provided by the server,
  it = headers.find("version");
  if (it == headers.end()) {
    LOG(ERROR) << "SpdyHeaderBlock without version header.";
    return false;
  }
  version = it->second;

  response->response_time = base::Time::Now();

  std::string raw_headers(version);
  raw_headers.push_back(' ');
  raw_headers.append(status);
  raw_headers.push_back('\0');
  for (it = headers.begin(); it != headers.end(); ++it) {
    // For each value, if the server sends a NUL-separated
    // list of values, we separate that back out into
    // individual headers for each value in the list.
    // e.g.
    //    Set-Cookie "foo\0bar"
    // becomes
    //    Set-Cookie: foo\0
    //    Set-Cookie: bar\0
    std::string value = it->second;
    size_t start = 0;
    size_t end = 0;
    do {
      end = value.find('\0', start);
      std::string tval;
      if (end != value.npos)
        tval = value.substr(start, (end - start));
      else
        tval = value.substr(start);
      raw_headers.append(it->first);
      raw_headers.push_back(':');
      raw_headers.append(tval);
      raw_headers.push_back('\0');
      start = end + 1;
    } while (end != value.npos);
  }

  response->headers = new HttpResponseHeaders(raw_headers);
  response->was_fetched_via_spdy = true;
  return true;
}

void CreateSpdyHeadersFromHttpRequest(
    const HttpRequestInfo& info, spdy::SpdyHeaderBlock* headers,
    bool direct) {
  // TODO(willchan): It's not really necessary to convert from
  // HttpRequestHeaders to spdy::SpdyHeaderBlock.

  static const char kHttpProtocolVersion[] = "HTTP/1.1";

  HttpRequestHeaders::Iterator it(info.extra_headers);

  while (it.GetNext()) {
    std::string name = StringToLowerASCII(it.name());
    if (headers->find(name) == headers->end()) {
      (*headers)[name] = it.value();
    } else {
      std::string new_value = (*headers)[name];
      new_value.append(1, '\0');  // +=() doesn't append 0's
      new_value += it.value();
      (*headers)[name] = new_value;
    }
  }

  // TODO(rch): Add Proxy headers here. (See http_network_transaction.cc)
  // TODO(rch): Add authentication headers here.

  (*headers)["method"] = info.method;

  // Handle content-length. This is the same as BuildRequestHeader in
  // http_network_transaction.cc.
  // TODO(lzheng): reduce the code duplication between spdy and http here.
  if (info.upload_data) {
    (*headers)["content-length"] =
        base::Int64ToString(info.upload_data->GetContentLength());
  } else if (info.method == "POST" || info.method == "PUT" ||
             info.method == "HEAD") {
    // An empty POST/PUT request still needs a content length.  As for HEAD,
    // IE and Safari also add a content length header.  Presumably it is to
    // support sending a HEAD request to an URL that only expects to be sent a
    // POST or some other method that normally would have a message body.
    (*headers)["content-length"] = "0";
  }

  if (direct)
    (*headers)["url"] = HttpUtil::PathForRequest(info.url);
  else
    (*headers)["url"] = HttpUtil::SpecForRequest(info.url);
  (*headers)["host"] = GetHostAndOptionalPort(info.url);
  (*headers)["scheme"] = info.url.scheme();
  (*headers)["version"] = kHttpProtocolVersion;
  if (!info.referrer.is_empty())
    (*headers)["referer"] = info.referrer.spec();

  // Honor load flags that impact proxy caches.
  if (info.load_flags & LOAD_BYPASS_CACHE) {
    (*headers)["pragma"] = "no-cache";
    (*headers)["cache-control"] = "no-cache";
  } else if (info.load_flags & LOAD_VALIDATE_CACHE) {
    (*headers)["cache-control"] = "max-age=0";
  }
}

}  // namespace net