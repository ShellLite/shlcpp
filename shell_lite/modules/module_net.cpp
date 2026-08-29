#include "../native_registry.hpp"
#include <curl/curl.h>
#include <string>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace shell_lite {

static std::once_flag g_curl_init_flag;

static void ensure_curl_initialized() {
    std::call_once(g_curl_init_flag, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string net_download_impl(std::string url, std::optional<double> timeout_sec) {
    ensure_curl_initialized();
    long timeout = timeout_sec.has_value() ? (long)timeout_sec.value() : 30L;

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize libcurl");
    }

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string err_msg = "Download failed: " + std::string(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        throw std::runtime_error(err_msg);
    }
    curl_easy_cleanup(curl);
    return readBuffer;
}

static std::string net_post_impl(std::string url, std::string post_data, std::optional<double> timeout_sec) {
    ensure_curl_initialized();
    long timeout = timeout_sec.has_value() ? (long)timeout_sec.value() : 30L;

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize libcurl");
    }

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string err_msg = "POST failed: " + std::string(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        throw std::runtime_error(err_msg);
    }
    curl_easy_cleanup(curl);
    return readBuffer;
}

void register_stdlib_net(VM* vm) {
    ensure_curl_initialized();
    NativeRegistry::bind(vm, "std_net_download", net_download_impl);
    NativeRegistry::bind(vm, "std_net_get", net_download_impl);
    NativeRegistry::bind(vm, "http_get", net_download_impl);

    NativeRegistry::bind(vm, "std_net_post", net_post_impl);
    NativeRegistry::bind(vm, "http_post", net_post_impl);
}

} // namespace shell_lite
