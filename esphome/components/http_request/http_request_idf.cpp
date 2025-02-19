#include "http_request_idf.h"

#ifdef USE_ESP_IDF

#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "esp_task_wdt.h"

namespace esphome {
namespace http_request {

static const char *const TAG = "http_request.idf";

void HttpRequestIDF::dump_config() {
  HttpRequestComponent::dump_config();
  ESP_LOGCONFIG(TAG, "  Buffer Size RX: %u", this->buffer_size_rx_);
  ESP_LOGCONFIG(TAG, "  Buffer Size TX: %u", this->buffer_size_tx_);
}

std::shared_ptr<HttpContainer> HttpRequestIDF::start(std::string url, std::string method, std::string body,
                                                     std::list<Header> headers) {
  if (!network::is_connected()) {
    this->status_momentary_error("failed", 1000);
    ESP_LOGE(TAG, "HTTP Request failed; Not connected to network");
    return nullptr;
  }

  esp_http_client_method_t method_idf;
  if (method == "GET") {
    method_idf = HTTP_METHOD_GET;
  } else if (method == "POST") {
    method_idf = HTTP_METHOD_POST;
  } else if (method == "PUT") {
    method_idf = HTTP_METHOD_PUT;
  } else if (method == "DELETE") {
    method_idf = HTTP_METHOD_DELETE;
  } else if (method == "PATCH") {
    method_idf = HTTP_METHOD_PATCH;
  } else {
    this->status_momentary_error("failed", 1000);
    ESP_LOGE(TAG, "HTTP Request failed; Unsupported method");
    return nullptr;
  }

  bool secure = url.find("https:") != std::string::npos;

  esp_http_client_config_t config = {};

  config.url = url.c_str();
  config.method = method_idf;
  config.timeout_ms = this->timeout_;
  config.disable_auto_redirect = !this->follow_redirects_;
  config.max_redirection_count = this->redirect_limit_;
  config.auth_type = HTTP_AUTH_TYPE_BASIC;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  if (secure) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }
#endif

  if (this->useragent_ != nullptr) {
    config.user_agent = this->useragent_;
  }

  config.buffer_size = this->buffer_size_rx_;
  config.buffer_size_tx = this->buffer_size_tx_;

  const uint32_t start = millis();
  watchdog::WatchdogManager wdm(this->get_watchdog_timeout());

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    this->status_momentary_error("failed", 1000);
    ESP_LOGE(TAG, "HTTP Request failed; Could not initialize client");
    return nullptr;
  }

  std::shared_ptr<HttpContainerIDF> container = std::make_shared<HttpContainerIDF>(client);
  container->set_parent(this);
  container->set_secure(secure);

  for (const auto &header : headers) {
    esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
  }

  const int body_len = body.length();

  esp_err_t err = esp_http_client_open(client, body_len);
  if (err != ESP_OK) {
    this->status_momentary_error("failed", 1000);
    ESP_LOGE(TAG, "HTTP Request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return nullptr;
  }

  if (body_len > 0) {
    int write_left = body_len;
    int write_index = 0;
    const char *buf = body.c_str();
    while (write_left > 0) {
      container->feed_wdt();  // Feed watchdog during long writes
      int written = esp_http_client_write(client, buf + write_index, write_left);
      if (written < 0) {
        err = ESP_FAIL;
        break;
      }
      write_left -= written;
      write_index += written;
    }
  }

  if (err != ESP_OK) {
    this->status_momentary_error("failed", 1000);
    ESP_LOGE(TAG, "HTTP Request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return nullptr;
  }

  container->feed_wdt();
  // Safely fetch headers with error checking
  int content_len = 0;
  content_len = esp_http_client_fetch_headers(client);
  // Check for fetch headers error
  if (content_len < 0 && content_len != -1) {  // -1 is valid for chunked encoding
    this->status_momentary_error("failed", 1000);
    ESP_LOGE(TAG, "HTTP Request failed while fetching headers");
    esp_http_client_cleanup(client);
    return nullptr;
  }
  container->content_length = content_len;
  container->feed_wdt();
  // Safely get status code
  container->status_code = esp_http_client_get_status_code(client);
  container->feed_wdt();
  // Handle successful status code
  if (is_success(container->status_code)) {
    container->duration_ms = millis() - start;
    return container;
  }

  // Handle connection error (-1) specially to prevent crash
  if (container->status_code == -1) {
    ESP_LOGE(TAG, "HTTP Request failed with connection error; URL: %s; Code: %d", url.c_str(), container->status_code);
    this->status_momentary_error("failed", 1000);
    esp_http_client_cleanup(client);
    return nullptr;
  }

  if (this->follow_redirects_) {
    auto num_redirects = this->redirect_limit_;
    while (is_redirect(container->status_code) && num_redirects > 0) {
      container->feed_wdt();
      err = esp_http_client_set_redirection(client);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_set_redirection failed: %s", esp_err_to_name(err));
        this->status_momentary_error("failed", 1000);
        esp_http_client_cleanup(client);
        return nullptr;
      }
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
      char redirect_url[256]{};
      if (esp_http_client_get_url(client, redirect_url, sizeof(redirect_url) - 1) == ESP_OK) {
        ESP_LOGV(TAG, "redirecting to url: %s", redirect_url);
      }
#endif
      err = esp_http_client_open(client, 0);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        this->status_momentary_error("failed", 1000);
        esp_http_client_cleanup(client);
        return nullptr;
      }

      container->feed_wdt();
      container->content_length = esp_http_client_fetch_headers(client);
      container->feed_wdt();
      container->status_code = esp_http_client_get_status_code(client);
      container->feed_wdt();
      if (is_success(container->status_code)) {
        container->duration_ms = millis() - start;
        return container;
      }

      num_redirects--;
    }

    if (num_redirects == 0) {
      ESP_LOGW(TAG, "Reach redirect limit count=%d", this->redirect_limit_);
    }
  }

  ESP_LOGE(TAG, "HTTP Request failed; URL: %s; Code: %d", url.c_str(), container->status_code);
  this->status_momentary_error("failed", 1000);
  // Don't clean up client here - let the container handle it
  return container;
}

int HttpContainerIDF::read(uint8_t *buf, size_t max_len) {
  if (this->client_ == nullptr) {
    ESP_LOGE("http_request.idf", "Attempted to read from null client");
    return -1;
  }

  const uint32_t start = millis();
  watchdog::WatchdogManager wdm(this->parent_->get_watchdog_timeout());

  int bufsize = std::min(max_len, this->content_length - this->bytes_read_);

  if (bufsize == 0) {
    this->duration_ms += (millis() - start);
    return 0;
  }

  this->feed_wdt();
  int read_len = esp_http_client_read(this->client_, (char *) buf, bufsize);
  this->feed_wdt();
  if (read_len >= 0) {
    this->bytes_read_ += read_len;
  }

  this->duration_ms += (millis() - start);

  return read_len;
}

void HttpContainerIDF::end() {
  if (this->client_ == nullptr) {
    return;  // Already cleaned up
  }
  watchdog::WatchdogManager wdm(this->parent_->get_watchdog_timeout());

  esp_http_client_close(this->client_);
  this->feed_wdt();
  esp_http_client_cleanup(this->client_);
  this->client_ = nullptr;  // Mark as cleaned up
}

void HttpContainerIDF::feed_wdt() {
  // Tests to see if the executing task has a watchdog timer attached
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    App.feed_wdt();
  }
}

HttpContainerIDF::~HttpContainerIDF() {
  if (this->client_ != nullptr) {
    // Safety net - ensure cleanup if container is destroyed
    esp_http_client_close(this->client_);
    esp_http_client_cleanup(this->client_);
    this->client_ = nullptr;
  }
}

}  // namespace http_request
}  // namespace esphome

#endif  // USE_ESP_IDF
