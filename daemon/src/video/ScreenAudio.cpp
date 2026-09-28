#include "video/ScreenAudio.hpp"

#include "omachat/core/Log.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/dict.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <set>
#include <vector>

#include <unistd.h>

namespace omachat::video {

// ================================================================== tone

ToneAudioSource::ToneAudioSource(double hz, QObject* parent)
    : ScreenAudioSource(parent)
    , m_hz(hz)
{
}

ToneAudioSource::~ToneAudioSource()
{
    stop();
}

bool ToneAudioSource::start(SamplesFn onSamples, QString*)
{
    stop();
    m_running = true;
    m_thread = std::thread([this, onSamples = std::move(onSamples)] {
        std::vector<float> chunk(480); // 10 ms
        double phase = 0;
        auto next = std::chrono::steady_clock::now();
        while (m_running) {
            for (float& x : chunk) {
                x = static_cast<float>(0.3 * std::sin(phase));
                phase += 2.0 * std::numbers::pi * m_hz / 48000.0;
            }
            onSamples(chunk.data(), chunk.size());
            next += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next);
        }
    });
    return true;
}

void ToneAudioSource::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

// ======================================================= application audio

namespace {

class ApplicationAudioSource : public ScreenAudioSource {
public:
    explicit ApplicationAudioSource(QObject* parent)
        : ScreenAudioSource(parent)
    {
        pw_init(nullptr, nullptr);
    }
    ~ApplicationAudioSource() override { stop(); }

    QString name() const override { return QStringLiteral("applications"); }
    int applications() const override { return m_linkedNodes.load(); }

    bool start(SamplesFn onSamples, QString* error) override
    {
        stop();
        m_onSamples = std::move(onSamples);
        m_loop = pw_thread_loop_new("omachat-screen-audio", nullptr);
        m_context = m_loop ? pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0) : nullptr;
        if (!m_context || pw_thread_loop_start(m_loop) < 0) {
            *error = QStringLiteral("PipeWire is not available");
            stop();
            return false;
        }
        pw_thread_loop_lock(m_loop);
        m_core = pw_context_connect(m_context, nullptr, 0);
        if (!m_core) {
            pw_thread_loop_unlock(m_loop);
            *error = QStringLiteral("cannot connect to PipeWire");
            stop();
            return false;
        }
        // Not auto-connected: it only hears what we link into it. Marked as
        // OmaChat so another OmaChat never captures it either.
        m_stream = pw_stream_new(m_core, "OmaChat Screen Audio",
            pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen",
                PW_KEY_APP_NAME, "OmaChat", PW_KEY_APP_ID, "org.omarchy.OmaChat", PW_KEY_NODE_NAME,
                "omachat-screen-audio", PW_KEY_NODE_LATENCY, "480/48000", nullptr));
        static const pw_stream_events streamEvents = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.process = &ApplicationAudioSource::onProcess;
            e.state_changed = &ApplicationAudioSource::onStreamState;
            return e;
        }();
        pw_stream_add_listener(m_stream, &m_streamListener, &streamEvents, this);
        std::uint8_t buffer[512];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = 48000;
        info.channels = 1;
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
        const int rc = pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);

        m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
        static const pw_registry_events registryEvents = [] {
            pw_registry_events e{};
            e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = &ApplicationAudioSource::onGlobal;
            e.global_remove = &ApplicationAudioSource::onGlobalRemove;
            return e;
        }();
        pw_registry_add_listener(m_registry, &m_registryListener, &registryEvents, this);
        pw_thread_loop_unlock(m_loop);
        if (rc < 0) {
            *error = QStringLiteral("cannot open the screen audio capture");
            stop();
            return false;
        }
        OMA_INFO("video", "capturing application audio for the screen share");
        return true;
    }

    void stop() override
    {
        if (m_loop)
            pw_thread_loop_stop(m_loop);
        for (auto& [port, link] : m_links)
            pw_proxy_destroy(link);
        m_links.clear();
        if (m_registry) {
            spa_hook_remove(&m_registryListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_registry));
            m_registry = nullptr;
        }
        if (m_stream) {
            spa_hook_remove(&m_streamListener);
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
        }
        if (m_core) {
            pw_core_disconnect(m_core);
            m_core = nullptr;
        }
        if (m_context) {
            pw_context_destroy(m_context);
            m_context = nullptr;
        }
        if (m_loop) {
            pw_thread_loop_destroy(m_loop);
            m_loop = nullptr;
        }
        m_appNodes.clear();
        m_outputPorts.clear();
        m_ourNode = m_ourPort = 0;
        m_linkedNodes = 0;
    }

private:
    // ---- everything below runs on the PipeWire loop thread

    static bool isOurs(const spa_dict* props)
    {
        const char* app = spa_dict_lookup(props, PW_KEY_APP_ID);
        const char* pid = spa_dict_lookup(props, PW_KEY_APP_PROCESS_ID);
        return (app && std::strcmp(app, "org.omarchy.OmaChat") == 0)
            || (pid && std::atoi(pid) == static_cast<int>(::getpid()));
    }

    static void onGlobal(
        void* data, std::uint32_t id, std::uint32_t, const char* type, std::uint32_t, const spa_dict* props)
    {
        auto* self = static_cast<ApplicationAudioSource*>(data);
        if (!props)
            return;
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
            if (mediaClass && std::strcmp(mediaClass, "Stream/Output/Audio") == 0 && !isOurs(props)) {
                self->m_appNodes.insert(id);
                self->linkAll();
            }
            return;
        }
        if (std::strcmp(type, PW_TYPE_INTERFACE_Port) != 0)
            return;
        const char* node = spa_dict_lookup(props, PW_KEY_NODE_ID);
        const char* dir = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
        if (!node || !dir)
            return;
        const auto nodeId = static_cast<std::uint32_t>(std::strtoul(node, nullptr, 10));
        const char* monitor = spa_dict_lookup(props, PW_KEY_PORT_MONITOR);
        if (std::strcmp(dir, "out") == 0 && !(monitor && std::strcmp(monitor, "true") == 0))
            self->m_outputPorts[id] = nodeId;
        else if (std::strcmp(dir, "in") == 0 && self->m_stream && nodeId == pw_stream_get_node_id(self->m_stream)) {
            self->m_ourNode = nodeId;
            self->m_ourPort = id;
        }
        self->linkAll();
    }

    static void onGlobalRemove(void* data, std::uint32_t id)
    {
        auto* self = static_cast<ApplicationAudioSource*>(data);
        self->m_appNodes.erase(id);
        self->m_outputPorts.erase(id);
        if (auto it = self->m_links.find(id); it != self->m_links.end()) {
            pw_proxy_destroy(it->second);
            self->m_links.erase(it);
        }
        self->countNodes();
    }

    void linkAll()
    {
        if (!m_ourPort)
            return;
        for (const auto& [port, node] : m_outputPorts) {
            if (!m_appNodes.contains(node) || m_links.contains(port))
                continue;
            pw_properties* p = pw_properties_new(PW_KEY_OBJECT_LINGER, "false", nullptr);
            pw_properties_setf(p, PW_KEY_LINK_OUTPUT_NODE, "%u", node);
            pw_properties_setf(p, PW_KEY_LINK_OUTPUT_PORT, "%u", port);
            pw_properties_setf(p, PW_KEY_LINK_INPUT_NODE, "%u", m_ourNode);
            pw_properties_setf(p, PW_KEY_LINK_INPUT_PORT, "%u", m_ourPort);
            auto* link = static_cast<pw_proxy*>(
                pw_core_create_object(m_core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &p->dict, 0));
            pw_properties_free(p);
            if (link)
                m_links[port] = link;
        }
        countNodes();
    }

    void countNodes()
    {
        std::set<std::uint32_t> nodes;
        for (const auto& [port, link] : m_links)
            if (auto it = m_outputPorts.find(port); it != m_outputPorts.end())
                nodes.insert(it->second);
        m_linkedNodes = static_cast<int>(nodes.size());
    }

    static void onStreamState(void* data, pw_stream_state, pw_stream_state, const char*)
    {
        auto* self = static_cast<ApplicationAudioSource*>(data);
        self->linkAll(); // our input port may exist now
    }

    static void onProcess(void* data)
    {
        auto* self = static_cast<ApplicationAudioSource*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->m_stream);
        if (!b)
            return;
        const spa_data& d = b->buffer->datas[0];
        if (d.data && d.chunk && d.chunk->size > 0 && self->m_onSamples)
            self->m_onSamples(
                static_cast<const float*>(SPA_PTROFF(d.data, d.chunk->offset, void)), d.chunk->size / sizeof(float));
        pw_stream_queue_buffer(self->m_stream, b);
    }

    SamplesFn m_onSamples;
    pw_thread_loop* m_loop = nullptr;
    pw_context* m_context = nullptr;
    pw_core* m_core = nullptr;
    pw_stream* m_stream = nullptr;
    pw_registry* m_registry = nullptr;
    spa_hook m_streamListener{};
    spa_hook m_registryListener{};
    std::set<std::uint32_t> m_appNodes;
    std::map<std::uint32_t, std::uint32_t> m_outputPorts; // port -> node
    std::map<std::uint32_t, pw_proxy*> m_links; // app port -> link
    std::uint32_t m_ourNode = 0, m_ourPort = 0;
    std::atomic<int> m_linkedNodes{0};
};

} // namespace

std::unique_ptr<ScreenAudioSource> makeApplicationAudioSource(QObject* parent)
{
    return std::make_unique<ApplicationAudioSource>(parent);
}

} // namespace omachat::video
