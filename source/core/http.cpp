#include "cJSON.h"
#include "retrosync/api.hpp"
#include <algorithm>
#include <cstdio>
#include <curl/curl.h>
#include <unistd.h>

namespace rs {
namespace {
struct Transfer {
  HttpResponse result;
  Progress *progress;
  FILE *file = nullptr;
  uint64_t maximum = MaxJsonBytes, count = 0;
  std::atomic<bool> *legacyCancel = nullptr;
  std::atomic<unsigned> *legacyReceived = nullptr;
};
bool cancelled(const Transfer &t) {
  return t.progress->cancel.load() ||
         (t.legacyCancel && t.legacyCancel->load());
}
size_t receive(char *data, size_t size, size_t count, void *user) {
  auto &t = *static_cast<Transfer *>(user);
  if (cancelled(t))
    return 0;
  if (size && count > t.maximum / size) {
    t.result.tooLarge = true;
    return 0;
  }
  size_t n = size * count;
  if (n > t.maximum - t.count) {
    t.result.tooLarge = true;
    return 0;
  }
  if (t.file) {
    if (std::fwrite(data, 1, n, t.file) != n) {
      t.result.error = "SD write failed; check free space";
      return 0;
    }
  } else
    t.result.body.append(data, n);
  t.count += n;
  t.progress->current.store(t.count);
  if (t.legacyReceived)
    t.legacyReceived->store(static_cast<unsigned>(t.count));
  return n;
}
int progress(void *user, curl_off_t total, curl_off_t, curl_off_t uploadTotal,
             curl_off_t uploaded) {
  auto &t = *static_cast<Transfer *>(user);
  if (uploadTotal > 0) {
    t.progress->total.store(uploadTotal);
    t.progress->current.store(uploaded);
  } else if (total > 0)
    t.progress->total.store(total);
  return cancelled(t) ? 1 : 0;
}
HttpResponse perform(const Settings &s, const std::string &path,
                     const std::string &ca, Progress &p, const Request &req,
                     FILE *file = nullptr, uint64_t maximum = MaxJsonBytes,
                     std::atomic<bool> *oldCancel = nullptr,
                     std::atomic<unsigned> *oldReceived = nullptr) {
  Transfer t{};
  t.progress = &p;
  t.file = file;
  t.maximum = maximum;
  t.legacyCancel = oldCancel;
  t.legacyReceived = oldReceived;
  p.current.store(0);
  p.total.store(0);
  Settings checked = s;
  if (req.anonymous)
    checked.token = "anonymous";
  if (!validateSettings(checked, t.result.error))
    return t.result;
  if (path.compare(0, 5, "/api/") != 0 ||
      path.find_first_of("\r\n\\#") != std::string::npos ||
      path.find("/../") != std::string::npos) {
    t.result.error = "Invalid API path";
    return t.result;
  }
  CURL *c = curl_easy_init();
  if (!c) {
    t.result.error = "Cannot allocate HTTP client";
    return t.result;
  }
  std::string url = s.server + path, auth = "Authorization: Bearer " + s.token;
  curl_slist *headers = nullptr;
  if (!req.anonymous)
    headers = curl_slist_append(headers, auth.c_str());
  headers = curl_slist_append(headers, "Expect:");
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "RetroSync3DS/1.0.0");
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
#else
  curl_easy_setopt(c, CURLOPT_PROTOCOLS,
                   static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(c, CURLOPT_PROXY, "");
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
  if (!ca.empty())
    curl_easy_setopt(c, CURLOPT_CAINFO, ca.c_str());
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT,
                   file || !req.uploadFile.empty() ? 600L : 25L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 128L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
  curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
  curl_mime *mime = nullptr;
  if (!req.uploadFile.empty()) {
    mime = curl_mime_init(c);
    auto *part = curl_mime_addpart(mime);
    curl_mime_name(part, "saveFile");
    curl_mime_filedata(part, req.uploadFile.c_str());
    curl_mime_filename(part, "retrosync.sav");
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
  } else if (req.method == "POST") {
    headers = curl_slist_append(headers,
                                ("Content-Type: " + req.contentType).c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(req.body.size()));
  }
  auto rc = curl_easy_perform(c);
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &t.result.status);
  if (cancelled(t))
    t.result.error = "Request cancelled";
  else if (t.result.tooLarge)
    t.result.error = file ? "Download exceeds supported size"
                          : "Response exceeds 512 KiB; try a smaller page";
  else if (t.result.error.empty()) {
    if (rc == CURLE_PEER_FAILED_VERIFICATION)
      t.result.error = "TLS certificate verification failed; check CA, "
                       "hostname and console clock";
    else if (rc == CURLE_SSL_CACERT_BADFILE)
      t.result.error = "TLS CA file missing or invalid";
    else if (rc != CURLE_OK)
      t.result.error = std::string("Network: ") + curl_easy_strerror(rc);
    else if (t.result.status == 401)
      t.result.error =
          "Authentication failed or session expired; sign in again";
    else if (t.result.status == 403)
      t.result.error = "Permission denied; check account permissions";
    else if (t.result.status == 409)
      t.result.error = "Remote save changed; review the conflict again";
    else if (t.result.status == 404 || t.result.status == 405)
      t.result.error = "Endpoint unavailable on this server/version";
    else if (t.result.status >= 300 && t.result.status < 400)
      t.result.error = "Redirect refused; configure the final server URL";
    else if (t.result.status != 200 && t.result.status != 201)
      t.result.error =
          "Server returned HTTP " + std::to_string(t.result.status);
  }
  curl_mime_free(mime);
  curl_slist_free_all(headers);
  curl_easy_cleanup(c);
  if (!t.result.error.empty())
    t.result.body.clear();
  return t.result;
}
bool applyTokens(Settings &s, const HttpResponse &r, std::string &err) {
  if (!r.error.empty()) {
    err = r.error;
    return false;
  }
  cJSON *j = cJSON_ParseWithLength(r.body.c_str(), r.body.size());
  const auto *a = cJSON_GetObjectItemCaseSensitive(j, "access_token");
  const auto *f = cJSON_GetObjectItemCaseSensitive(j, "refresh_token");
  const auto *type = cJSON_GetObjectItemCaseSensitive(j, "token_type");
  bool ok = cJSON_IsString(a) && cJSON_IsString(f) && cJSON_IsString(type) &&
            std::string(type->valuestring) == "bearer";
  Settings next = s;
  if (ok) {
    next.token = a->valuestring;
    next.refreshToken = f->valuestring;
    ok = validateSettings(next, err);
  }
  cJSON_Delete(j);
  if (!ok) {
    if (err.empty())
      err = "Unsupported login response";
    return false;
  }
  s = std::move(next);
  return true;
}
bool refresh(Settings &s, const std::string &ca, Progress &p,
             std::string &err) {
  Request r;
  r.method = "POST";
  r.anonymous = true;
  r.contentType = "application/x-www-form-urlencoded";
  r.body =
      "grant_type=refresh_token&refresh_token=" + urlEncode(s.refreshToken);
  auto response = perform(s, "/api/token", ca, p, r);
  bool ok = applyTokens(s, response, err);
  std::fill(r.body.begin(), r.body.end(), '\0');
  if (ok && !s.sessionPath.empty())
    ok = saveSettings(s.sessionPath, s, err);
  return ok;
}
} // namespace
HttpResponse getJson(const Settings &s, const std::string &path,
                     const std::string &ca, std::atomic<bool> &cancel,
                     std::atomic<unsigned> &received) {
  Progress p;
  return perform(s, path, ca, p, {}, nullptr, MaxJsonBytes, &cancel, &received);
}
HttpResponse requestJson(Settings &s, const std::string &path,
                         const std::string &ca, Progress &p, const Request &r) {
  auto response = perform(s, path, ca, p, r);
  // A 401 means the protected handler did not run. Never retry an uncertain
  // write or network failure.
  if (response.status == 401 && !r.anonymous && !s.refreshToken.empty() &&
      !p.cancel.load()) {
    std::string err;
    if (refresh(s, ca, p, err))
      return perform(s, path, ca, p, r);
    response.error = err;
  }
  return response;
}
bool signIn(Settings &s, const std::string &user, const std::string &password,
            const std::string &ca, Progress &p, std::string &err) {
  if (user.empty() || user.size() > 128 || password.empty() ||
      password.size() > 512) {
    err = "Enter username and password";
    return false;
  }
  Request r;
  r.method = "POST";
  r.anonymous = true;
  r.contentType = "application/x-www-form-urlencoded";
  r.body = "grant_type=password&username=" + urlEncode(user) +
           "&password=" + urlEncode(password) + "&scope=" +
           urlEncode("me.read platforms.read roms.read assets.read "
                     "assets.write devices.read devices.write");
  auto response = perform(s, "/api/token", ca, p, r);
  Settings next = s;
  next.username = user;
  bool ok = applyTokens(next, response, err);
  std::fill(r.body.begin(), r.body.end(), '\0');
  if (ok) {
    s = std::move(next);
    if (!s.sessionPath.empty())
      ok = saveSettings(s.sessionPath, s, err);
  }
  return ok;
}
bool getRomPage(Settings &s, int platform, unsigned offset,
                const std::string &search, unsigned limit,
                const std::string &ca, Progress &p, RomPage &page,
                std::string &err) {
  limit = std::max(1u, std::min(PageSize, limit));
  while (!p.cancel.load()) {
    auto r =
        requestJson(s, romListPath(platform, offset, search, limit), ca, p);
    if (r.tooLarge && limit > 1) {
      limit = limit > 5 ? 5 : 1;
      continue;
    }
    if (!r.error.empty()) {
      err = r.error;
      return false;
    }
    if (!parseRoms(r.body, page, err))
      return false;
    if (page.offset != offset || page.items.size() > limit ||
        (page.items.empty() && offset < page.total)) {
      err = "Server returned an inconsistent page";
      return false;
    }
    page.limit = limit;
    return true;
  }
  err = "Request cancelled";
  return false;
}
bool downloadFile(Settings &s, const std::string &path,
                  const std::string &output, uint64_t maximum,
                  uint64_t expected, const std::string &ca, Progress &p,
                  std::string &err) {
  // A caller-owned, unique staging name; never truncate an existing file.
  FILE *f = std::fopen(output.c_str(), "wbx");
  if (!f) {
    err = "Cannot create SD staging file";
    return false;
  }
  auto r = perform(s, path, ca, p, {}, f, maximum);
  if (r.status == 401 && !s.refreshToken.empty() && !p.cancel.load()) {
    std::string why;
    if (refresh(s, ca, p, why)) {
      std::fclose(f);
      std::remove(output.c_str());
      f = std::fopen(output.c_str(), "wbx");
      if (f)
        r = perform(s, path, ca, p, {}, f, maximum);
      else
        r.error = "Cannot reopen SD staging file";
    } else
      r.error = why;
  }
  bool ok = r.error.empty();
  if (f) {
    if (std::fflush(f) != 0 || fsync(fileno(f)) != 0)
      ok = false;
    if (std::fclose(f) != 0)
      ok = false;
  }
  if (ok && expected && p.current.load() != expected) {
    ok = false;
    r.error = "Download size differs from server metadata";
  }
  if (!ok) {
    std::remove(output.c_str());
    err = r.error.empty() ? "Cannot flush SD staging file" : r.error;
  }
  return ok;
}
} // namespace rs
