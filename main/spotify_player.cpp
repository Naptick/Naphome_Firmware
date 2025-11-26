#include "spotify_player.h"
#include "kva_config_defaults.h"

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_KVA_SPOTIFY_USE_CSPOT

#include <AudioSink.h>
#include <BellHTTPServer.h>
#include <BellTask.h>
#include <BellUtils.h>
#include <MDNSService.h>
#include <civetweb.h>
#include <nlohmann/json.hpp>
#include <variant>

#include <CSpotContext.h>
#include <LoginBlob.h>
#include <SpircHandler.h>
#include <TrackPlayer.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <new>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "audio_player.h"
// ESP-IDF v4.4: SPIFFS functions are in esp_vfs.h and esp_spiffs.h
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "mdns.h"

extern "C" {
#include "esp_vfs.h"
}

namespace {

constexpr const char *TAG = "spotify_player";
constexpr const char *kDefaultCredsPath = "/spiffs/spotify_blob.json";

std::mutex s_spirc_mutex;
std::shared_ptr<cspot::SpircHandler> s_spirc_handler;
std::atomic<bool> s_player_ready{false};
std::atomic<int> s_volume_percent{50};
std::atomic<bool> s_volume_known{false};

int clamp_percent(int percent)
{
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

int spirc_volume_to_percent(int volume)
{
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 65535) {
        volume = 65535;
    }
    return clamp_percent((volume * 100 + 32767) / 65535);
}

int percent_to_spirc_volume(int percent)
{
    percent = clamp_percent(percent);
    return (percent * 65535) / 100;
}

void set_spirc_handler(std::shared_ptr<cspot::SpircHandler> handler)
{
    std::lock_guard<std::mutex> lock(s_spirc_mutex);
    s_spirc_handler = handler;
    s_player_ready.store(handler != nullptr);
    if (!handler) {
        s_volume_known.store(false);
    }
}

std::shared_ptr<cspot::SpircHandler> acquire_spirc_handler()
{
    std::lock_guard<std::mutex> lock(s_spirc_mutex);
    return s_spirc_handler;
}

bool ensure_spiffs()
{
    static bool mounted = false;
    if (mounted) {
        return true;
    }
    // Try "storage" partition first (our custom partition), fall back to NULL (default)
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",  // Try our custom partition
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        // Fall back to default partition if "storage" doesn't exist
        ESP_LOGW(TAG, "SPIFFS mount failed with 'storage' partition (%s), trying default partition", esp_err_to_name(err));
        conf.partition_label = NULL;  // Use default partition
        err = esp_vfs_spiffs_register(&conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS mount failed with default partition (%s)", esp_err_to_name(err));
            return false;
        }
    }
    mounted = true;
    ESP_LOGI(TAG, "SPIFFS mounted successfully at /spiffs");
    return true;
}

class KorvoAudioSink : public AudioSink {
  public:
    KorvoAudioSink() = default;
    bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) override
    {
        (void)bitDepth;
        sample_rate_ = sampleRate;
        channel_count_ = channelCount == 0 ? 2 : channelCount;
        return true;
    }

    void feedPCMFrames(const uint8_t *buffer, size_t bytes) override
    {
        if (!buffer || bytes == 0) {
            return;
        }
        // Prevent divide-by-zero: ensure channel_count_ and sizeof(int16_t) are valid
        size_t sample_size = sizeof(int16_t);
        size_t frame_size = sample_size > 0 && channel_count_ > 0 ? (sample_size * channel_count_) : 2;
        size_t frame_count = frame_size > 0 ? bytes / frame_size : 0;
        audio_player_submit_pcm(reinterpret_cast<const int16_t *>(buffer),
                                frame_count,
                                static_cast<int>(sample_rate_),
                                channel_count_);
    }

  private:
    uint32_t sample_rate_ = 44100;
    uint8_t channel_count_ = 2;
};

class SpotifyPlayerTask : public bell::Task {
  public:
    explicit SpotifyPlayerTask(const spotify_player_config_t &cfg)
        : bell::Task("cspot", 32 * 1024, 0, 1, false), cfg_(cfg)
    {
        // Set default logger before starting task (required by cspot)
        bell::setDefaultLogger();
        startTask();
    }

  private:
    spotify_player_config_t cfg_;

    std::string creds_path() const
    {
        if (cfg_.credentials_path && cfg_.credentials_path[0] != '\0') {
            return cfg_.credentials_path;
        }
        return kDefaultCredsPath;
    }

    bool load_blob_from_disk(std::shared_ptr<cspot::LoginBlob> blob)
    {
        if (!ensure_spiffs()) {
            return false;
        }
        std::ifstream file(creds_path(), std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        std::string json((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        if (json.empty()) {
            return false;
        }
        try {
            blob->loadJson(json);
            ESP_LOGI(TAG, "Loaded Spotify credentials from %s", creds_path().c_str());
            return true;
        } catch (...) {
            ESP_LOGW(TAG, "Failed to parse stored Spotify credentials");
            return false;
        }
    }

    void save_blob_to_disk(std::shared_ptr<cspot::LoginBlob> blob)
    {
        if (!ensure_spiffs()) {
            return;
        }
        std::ofstream file(creds_path(), std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            ESP_LOGW(TAG, "Unable to persist Spotify blob to %s", creds_path().c_str());
            return;
        }
        file << blob->toJson();
        ESP_LOGI(TAG, "Saved Spotify credentials to %s", creds_path().c_str());
    }

    void runTask() override
    {
        // Ensure SPIFFS is mounted before any file operations (required by cspot)
        if (!ensure_spiffs()) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS - cspot cannot load/save credentials");
        }
        
        // Initialize mDNS (required by cspot for zeroconf)
        mdns_init();
        std::string hostname = cfg_.device_name ? cfg_.device_name : "korvo-naptick";
        for (auto &ch : hostname) {
            if (ch == ' ') {
                ch = '-';
            }
        }
        mdns_hostname_set(hostname.c_str());
        ESP_LOGI(TAG, "cspot mDNS initialized with hostname: %s", hostname.c_str());

        auto blob = std::make_shared<cspot::LoginBlob>(hostname);
        bool have_blob = load_blob_from_disk(blob);
        if (!have_blob) {
            ESP_LOGI(TAG, "No saved credentials found, starting zeroconf pairing...");
            have_blob = run_zeroconf(blob);
            if (have_blob) {
                save_blob_to_disk(blob);
            }
        } else {
            ESP_LOGI(TAG, "Loaded Spotify credentials from disk");
        }

        if (have_blob) {
            start_session(blob);
        } else {
            ESP_LOGE(TAG, "Unable to obtain Spotify credentials");
        }
        vTaskDelete(NULL);
    }

    bool run_zeroconf(std::shared_ptr<cspot::LoginBlob> blob)
    {
        std::atomic<bool> got_blob = false;
        uint16_t http_port = cfg_.zeroconf_port ? cfg_.zeroconf_port : 8080;

        auto server = std::make_unique<bell::BellHTTPServer>(http_port);
        server->registerGet(
            "/spotify_info",
            [&server, blob](struct mg_connection *conn) {
                return server->makeJsonResponse(blob->buildZeroconfInfo());
            });
        server->registerPost(
            "/spotify_info",
            [&server, blob, &got_blob](struct mg_connection *conn) {
                nlohmann::json response;
                response["status"] = 101;
                response["spotifyError"] = 0;
                response["statusString"] = "ERROR-OK";

                auto request_info = mg_get_request_info(conn);
                if (request_info->content_length > 0) {
                    std::string body;
                    body.resize(request_info->content_length);
                    mg_read(conn, body.data(), request_info->content_length);

                    mg_header headers[10];
                    int num = mg_split_form_urlencoded(body.data(), headers, 10);
                    std::map<std::string, std::string> query;

                    for (int i = 0; i < num; ++i) {
                        query[headers[i].name] = headers[i].value;
                    }
                    blob->loadZeroconfQuery(query);
                    got_blob = true;
                }
                return server->makeJsonResponse(response.dump());
            });

        bell::MDNSService::registerService(
            blob->getDeviceName().c_str(),
            "_spotify-connect",
            "_tcp",
            "",
            http_port,
            {{"VERSION", "1.0"}, {"CPath", "/spotify_info"}, {"Stack", "SP"}});

        ESP_LOGI(TAG, "Waiting for Spotify app to provision credentials via zeroconf...");
        while (!got_blob.load()) {
            BELL_SLEEP_MS(500);
        }
        ESP_LOGI(TAG, "Received Spotify login blob over zeroconf");
        return true;
    }

    void start_session(std::shared_ptr<cspot::LoginBlob> blob)
    {
        // Logger already set in constructor
        ESP_LOGI(TAG, "Creating cspot Context from LoginBlob...");
        auto ctx = cspot::Context::createFromBlob(blob);
        
        ESP_LOGI(TAG, "Connecting to Spotify AP...");
        ctx->session->connectWithRandomAp();
        
        ESP_LOGI(TAG, "Authenticating with Spotify...");
        auto token = ctx->session->authenticate(blob);
        if (token.empty()) {
            ESP_LOGE(TAG, "Spotify authentication failed");
            return;
        }
        ESP_LOGI(TAG, "Spotify authentication successful");

        ESP_LOGI(TAG, "Starting cspot session task...");
        ctx->session->startTask();
        
        ESP_LOGI(TAG, "Creating SpircHandler...");
        auto handler = std::make_shared<cspot::SpircHandler>(ctx);
        handler->subscribeToMercury();
        set_spirc_handler(handler);

        ESP_LOGI(TAG, "Setting up audio sink...");
        auto sink = std::make_shared<KorvoAudioSink>();
        handler->getTrackPlayer()->setDataCallback(
            [sink](uint8_t *data, size_t bytes, std::string_view) -> size_t {
                sink->feedPCMFrames(data, bytes);
                return bytes;
            });

        handler->setEventHandler(
            [](std::unique_ptr<cspot::SpircHandler::Event> event) {
                if (!event) {
                    return;
                }
                switch (event->eventType) {
                    case cspot::SpircHandler::EventType::TRACK_INFO:
                        // Extract track info from event
                        if (std::holds_alternative<cspot::TrackInfo>(event->data)) {
                            auto trackInfo = std::get<cspot::TrackInfo>(event->data);
                            ESP_LOGI(TAG, "Spotify track: %s - %s", 
                                     trackInfo.name.c_str(), 
                                     trackInfo.artist.c_str());
                        } else {
                            ESP_LOGI(TAG, "Spotify track metadata updated");
                        }
                        break;
                    case cspot::SpircHandler::EventType::VOLUME:
                        if (std::holds_alternative<int>(event->data)) {
                            int spirc_volume = std::get<int>(event->data);
                            int pct = spirc_volume_to_percent(spirc_volume);
                            s_volume_percent.store(pct);
                            s_volume_known.store(true);
                            ESP_LOGI(TAG, "Spotify volume -> %d%%", pct);
                        }
                        break;
                    case cspot::SpircHandler::EventType::PLAY_PAUSE:
                        if (std::holds_alternative<bool>(event->data)) {
                            bool paused = std::get<bool>(event->data);
                            ESP_LOGI(TAG, "Spotify player %s", paused ? "paused" : "playing");
                        }
                        break;
                    case cspot::SpircHandler::EventType::DISC:
                        ESP_LOGW(TAG, "Spotify session lost; waiting for reconnect");
                        s_player_ready.store(false);
                        break;
                    case cspot::SpircHandler::EventType::PLAYBACK_START:
                        s_player_ready.store(true);
                        ESP_LOGI(TAG, "Spotify playback started");
                        break;
                    default:
                        break;
                }
            });

        ESP_LOGI(TAG, "Spotify Connect session started as %s", blob->getDeviceName().c_str());
        ESP_LOGI(TAG, "Entering cspot packet handling loop...");

        while (true) {
            ctx->session->handlePacket();
        }
    }
};

SpotifyPlayerTask *s_task = nullptr;

}  // namespace

#else

static const char *TAG = "spotify_player";

#endif

extern "C" esp_err_t spotify_player_start(const spotify_player_config_t *config)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    if (!config) {
        ESP_LOGE(TAG, "spotify_player_start: invalid config");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task) {
        ESP_LOGW(TAG, "spotify_player_start: task already running");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Creating SpotifyPlayerTask (device: %s, port: %d)", 
             config->device_name ? config->device_name : "default",
             config->zeroconf_port ? config->zeroconf_port : 8080);
    SpotifyPlayerTask *task = new (std::nothrow) SpotifyPlayerTask(*config);
    if (!task) {
        ESP_LOGE(TAG, "Failed to create SpotifyPlayerTask: out of memory");
        return ESP_ERR_NO_MEM;
    }
    s_task = task;
    ESP_LOGI(TAG, "SpotifyPlayerTask created successfully");
    return ESP_OK;
#else
    (void)config;
    ESP_LOGW(TAG, "cspot support disabled in menuconfig");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

extern "C" bool spotify_player_is_ready(void)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    return s_player_ready.load();
#else
    return false;
#endif
}

extern "C" esp_err_t spotify_player_pause(void)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    auto handler = acquire_spirc_handler();
    if (!handler) {
        ESP_LOGE(TAG, "cspot not ready");
        return ESP_ERR_INVALID_STATE;
    }
    handler->setPause(true);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

extern "C" esp_err_t spotify_player_resume(void)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    auto handler = acquire_spirc_handler();
    if (!handler) {
        ESP_LOGE(TAG, "cspot not ready");
        return ESP_ERR_INVALID_STATE;
    }
    handler->setPause(false);
    s_player_ready.store(true);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

extern "C" esp_err_t spotify_player_set_volume_percent(int percent)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    auto handler = acquire_spirc_handler();
    if (!handler) {
        ESP_LOGE(TAG, "cspot not ready");
        return ESP_ERR_INVALID_STATE;
    }
    percent = clamp_percent(percent);
    handler->setRemoteVolume(percent_to_spirc_volume(percent));
    s_volume_percent.store(percent);
    s_volume_known.store(true);
    return ESP_OK;
#else
    (void)percent;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

extern "C" esp_err_t spotify_player_volume_delta(int delta_percent)
{
#if CONFIG_KVA_SPOTIFY_USE_CSPOT
    auto handler = acquire_spirc_handler();
    if (!handler) {
        ESP_LOGE(TAG, "cspot not ready");
        return ESP_ERR_INVALID_STATE;
    }
    int current = s_volume_known.load() ? s_volume_percent.load() : 50;
    int target = clamp_percent(current + delta_percent);
    handler->setRemoteVolume(percent_to_spirc_volume(target));
    s_volume_percent.store(target);
    s_volume_known.store(true);
    return ESP_OK;
#else
    (void)delta_percent;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
