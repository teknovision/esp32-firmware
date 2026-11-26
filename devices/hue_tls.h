#pragma once
// Custom code: verified Hue calls (see README.md, "Custom code: verified Hue calls").
//
// What: one HTTPS request to the Hue bridge through ESP-IDF esp_http_client, with the bridge certificate checked
// against the two Hue root certificates below and against the "Hue bridge id" (the CN of the bridge certificate).
// Why not http_request: in ESPHome 2026.9.1 http_request has one verify_ssl for the whole device and no option to
// check a certificate whose name differs from the URL host. The bridge is reached by IP address but its
// certificate names the bridge id, so http_request could only run unverified and a LAN device posing as the
// bridge address could read the Hue key.
// Where: included from devices/room-light.yaml (esphome: includes:), used by the hue_put script and the
// "Test Hue link" button.
// What could break it: a bridge certificate under a root not listed here, a new bridge or a factory reset (new
// bridge id), a bridge certificate with SANs that do not hold the id, an ESPHome upgrade past 2026.9.1 (other
// ESP-IDF, other esp_http_client), too little free heap during the TLS handshake. Raising the ESP-IDF log level
// to DEBUG would make esp_http_client print the request headers, Hue key included ("Write header" in
// esp_http_client.c): keep it at the build default (ERROR).

#include <cctype>
#include <cstdlib>
#include <string>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_task_wdt.h"
#include "esp_tls.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"

#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace hue_tls {

static const char *const TAG = "hue";

// Same values http_request uses in packages/pull-update.yaml (timeout: 10s, watchdog_timeout: 15s). The request
// blocks the main loop like http_request does, so the task watchdog is raised for the duration of the call.
static constexpr int TIMEOUT_MS = 10000;
static constexpr uint32_t WATCHDOG_TIMEOUT_MS = 15000;

// The two Hue trust roots, byte-identical to huebridge_cacert.pem in openhab/openhab-addons
// (bundles/org.openhab.binding.hue/src/main/resources/), fingerprints listed in README.md.
// 1. C=NL, O=Philips Hue, CN=root-bridge, self-signed, EC P-256, valid 2017-01-01 to 2038-01-19,
//    SHA-256 F0:BD:8E:65:09:E8:2F:77:4D:63:BC:00:9D:53:88:C9:69:FE:3D:CF:7D:6D:54:1D:63:51:B7:2B:89:8D:8A:CF
// 2. C=NL, O=Signify Hue, CN=Hue Root CA 01, self-signed, EC P-256, valid 2025-02-25 to 2050-12-31,
//    SHA-256 D8:B8:94:48:B2:AF:8E:16:76:18:5A:C0:72:19:EE:9D:CB:C8:F0:1C:12:2A:02:6A:2A:4B:7B:5C:FE:03:28:B8
// Certificate dates are not checked in this build (MBEDTLS_HAVE_TIME_DATE is off), so no clock is needed.
static const char HUE_ROOTS_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
MIICMjCCAdigAwIBAgIUO7FSLbaxikuXAljzVaurLXWmFw4wCgYIKoZIzj0EAwIw
OTELMAkGA1UEBhMCTkwxFDASBgNVBAoMC1BoaWxpcHMgSHVlMRQwEgYDVQQDDAty
b290LWJyaWRnZTAiGA8yMDE3MDEwMTAwMDAwMFoYDzIwMzgwMTE5MDMxNDA3WjA5
MQswCQYDVQQGEwJOTDEUMBIGA1UECgwLUGhpbGlwcyBIdWUxFDASBgNVBAMMC3Jv
b3QtYnJpZGdlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEjNw2tx2AplOf9x86
aTdvEcL1FU65QDxziKvBpW9XXSIcibAeQiKxegpq8Exbr9v6LBnYbna2VcaK0G22
jOKkTqOBuTCBtjAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNV
HQ4EFgQUZ2ONTFrDT6o8ItRnKfqWKnHFGmQwdAYDVR0jBG0wa4AUZ2ONTFrDT6o8
ItRnKfqWKnHFGmShPaQ7MDkxCzAJBgNVBAYTAk5MMRQwEgYDVQQKDAtQaGlsaXBz
IEh1ZTEUMBIGA1UEAwwLcm9vdC1icmlkZ2WCFDuxUi22sYpLlwJY81Wrqy11phcO
MAoGCCqGSM49BAMCA0gAMEUCIEBYYEOsa07TH7E5MJnGw557lVkORgit2Rm1h3B2
sFgDAiEA1Fj/C3AN5psFMjo0//mrQebo0eKd3aWRx+pQY08mk48=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIBzDCCAXOgAwIBAgICEAAwCgYIKoZIzj0EAwIwPDELMAkGA1UEBhMCTkwxFDAS
BgNVBAoMC1NpZ25pZnkgSHVlMRcwFQYDVQQDDA5IdWUgUm9vdCBDQSAwMTAgFw0y
NTAyMjUwMDAwMDBaGA8yMDUwMTIzMTIzNTk1OVowPDELMAkGA1UEBhMCTkwxFDAS
BgNVBAoMC1NpZ25pZnkgSHVlMRcwFQYDVQQDDA5IdWUgUm9vdCBDQSAwMTBZMBMG
ByqGSM49AgEGCCqGSM49AwEHA0IABFfOO0jfSAUXGQ9kjEDzyBrcMQ3ItyA5krE+
cyvb1Y3xFti7KlAad8UOnAx0FBLn7HZrlmIwm1QnX0fK3LPM13mjYzBhMB0GA1Ud
DgQWBBTF1pSpsCASX/z0VHLigxU2CAaqoTAfBgNVHSMEGDAWgBTF1pSpsCASX/z0
VHLigxU2CAaqoTAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAKBggq
hkjOPQQDAgNHADBEAiAk7duT+IHbOGO4UUuGLAEpyYejGZK9Z7V9oSfnvuQ5BQIg
IYSgwwxHXm73/JgcU9lAM6c8Bmu3UE3kBIUwBs1qXFw=
-----END CERTIFICATE-----
)PEM";

// Negative values of Result::status: why the bridge gave no HTTP answer. Nothing with the key was sent for
// NOT_CONFIGURED, NO_NETWORK, CLIENT_ERROR, NOT_REACHABLE, CERT_REJECTED and TLS_FAILED.
enum Failure : int {
  NOT_CONFIGURED = -1,  // bridge, key or a 16 hex character bridge id missing
  NO_NETWORK = -2,      // Wi-Fi not connected
  CLIENT_ERROR = -3,    // esp_http_client could not be created (memory or invalid Hue bridge value)
  NOT_REACHABLE = -4,   // name lookup or TCP connect failed or timed out
  CERT_REJECTED = -5,   // TLS handshake failed on the certificate (unknown root, other bridge id, bad signature)
  TLS_FAILED = -6,      // TLS failed for another reason (setup, memory, protocol)
  NO_ANSWER = -7,       // verified connection, but the request or the answer failed (timeout, closed)
};

struct Result {
  int status;     // HTTP status (>= 100) when the bridge answered, else a negative Failure value
  esp_err_t err;  // esp_err for the log: ESP_OK when the bridge answered
};

// The bridge id is the CN of the bridge certificate: 16 hex characters. Case does not matter, mbedTLS compares
// the CN case-insensitively (x509_crt_check_cn in x509_crt.c).
inline bool valid_bridge_id(const std::string &id) {
  if (id.size() != 16)
    return false;
  for (char c : id) {
    if (!isxdigit((unsigned char) c))
      return false;
  }
  return true;
}

// Short status for the "Hue link" text sensor. Never holds a secret.
inline std::string describe(const Result &r) {
  if (r.status >= 200 && r.status < 300)
    return "ok, HTTP " + std::to_string(r.status);
  if (r.status >= 100)
    return "HTTP " + std::to_string(r.status);
  switch (r.status) {
    case NOT_CONFIGURED:
      return "not configured";
    case NO_NETWORK:
      return "no network";
    case CLIENT_ERROR:
      return "client error";
    case NOT_REACHABLE:
      return "bridge not reachable";
    case CERT_REJECTED:
      return "certificate rejected";
    case TLS_FAILED:
      return "TLS handshake failed";
    default:
      return "no answer from bridge";
  }
}

// Feeds the task watchdog between the stages of the blocking request, the same way http_request does
// (HttpContainerIDF::feed_wdt). Does not look at the event: headers and data are never logged.
inline esp_err_t feed_wdt_on_event(esp_http_client_event_t * /*evt*/) {
  if (esp_task_wdt_status(nullptr) == ESP_OK)
    esphome::App.feed_wdt();
  return ESP_OK;
}

// One request to https://<host>/clip/v2/resource/<path>. method is HTTP_METHOD_PUT for commands and
// HTTP_METHOD_GET for the link test. The Hue key goes only into the hue-application-key header. Fail closed:
// esp_http_client writes the request (headers included) only after esp-tls finished a handshake that verified
// the bridge certificate against HUE_ROOTS_PEM and the bridge id, so on any failure the key is not sent.
inline Result request(const std::string &host, const std::string &bridge_id, const std::string &key,
                      esp_http_client_method_t method, const std::string &path, const std::string &body) {
  Result r{NOT_CONFIGURED, ESP_OK};
  // Checked here too, not only by the caller: without a valid id there is no name to verify against.
  if (host.empty() || key.empty() || !valid_bridge_id(bridge_id)) {
    ESP_LOGW(TAG, "Hue request not sent: Hue bridge, Hue bridge id or Hue key missing");
    return r;
  }
  if (!esphome::network::is_connected()) {
    r.status = NO_NETWORK;
    ESP_LOGW(TAG, "Hue request not sent: no network");
    return r;
  }

  const std::string url = "https://" + host + "/clip/v2/resource/" + path;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = method;
  config.timeout_ms = TIMEOUT_MS;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  // A CA is given, so esp-tls verifies the chain with MBEDTLS_SSL_VERIFY_REQUIRED (set_ca_cert in
  // esp_tls_mbedtls.c) even though this build has CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y: that option only
  // applies when no CA, bundle or global store is given. crt_bundle_attach and use_global_ca_store stay unset
  // because esp-tls would prefer them over cert_pem. cert_len 0 means "PEM, use strlen + 1".
  config.cert_pem = HUE_ROOTS_PEM;
  // Name to match instead of the URL host (an IP address): mbedtls_ssl_set_hostname(common_name), which with no
  // SAN in the certificate is compared to the certificate CN. Also sent as SNI.
  config.common_name = bridge_id.c_str();
  // A redirect would resend the key header to wherever the answer points, so it is not followed.
  config.disable_auto_redirect = true;
  config.event_handler = feed_wdt_on_event;

  esphome::watchdog::WatchdogManager wdm(WATCHDOG_TIMEOUT_MS);
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    r.status = CLIENT_ERROR;
    ESP_LOGE(TAG, "Hue request failed: HTTP client could not be created");
    return r;
  }
  esp_http_client_set_header(client, "hue-application-key", key.c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  if (!body.empty())
    esp_http_client_set_post_field(client, body.data(), (int) body.size());

  const esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    r.status = esp_http_client_get_status_code(client);
    if (r.status >= 200 && r.status < 300) {
      ESP_LOGD(TAG, "Hue request ok: HTTP %d (%s)", r.status, path.c_str());
    } else {
      ESP_LOGW(TAG, "Hue request failed: HTTP %d (%s)", r.status, path.c_str());
    }
  } else {
    // esp-tls keeps the last error of the connection: last_error (esp_err), the mbedTLS code (stored as a
    // positive number) and the certificate verify flags (esp_tls_get_and_clear_last_error in esp_tls.c).
    int tls_code = 0, tls_flags = 0;
    const esp_err_t last = esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);
    const int mbed = -std::abs(tls_code);
    const bool cert_error = tls_flags != 0 || mbed == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ||
                            mbed == MBEDTLS_ERR_SSL_BAD_CERTIFICATE ||
                            (mbed <= -0x2080 && mbed >= -0x2980 && mbed != MBEDTLS_ERR_X509_ALLOC_FAILED);
    if (last == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME || last == ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET ||
        last == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST || last == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT ||
        last == ESP_ERR_ESP_TLS_SOCKET_SETOPT_FAILED || last == ESP_ERR_ESP_TLS_UNSUPPORTED_PROTOCOL_FAMILY) {
      r.status = NOT_REACHABLE;
    } else if (last == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED && cert_error) {
      r.status = CERT_REJECTED;
    } else if (err == ESP_ERR_HTTP_CONNECT) {
      // Only a failed connect means nothing was sent. A read or write error after the handshake also leaves a
      // TLS error behind, but by then the request went to the verified bridge: that is NO_ANSWER.
      r.status = TLS_FAILED;
    } else {
      r.status = NO_ANSWER;
    }
    r.err = last != ESP_OK ? last : err;
    ESP_LOGW(TAG, "Hue request failed: %s (%s, %s, mbedtls -0x%04X, cert flags 0x%x)", describe(r).c_str(),
             esp_err_to_name(err), esp_err_to_name(last), (unsigned) -mbed, (unsigned) tls_flags);
  }
  esp_http_client_cleanup(client);
  return r;
}

}  // namespace hue_tls
