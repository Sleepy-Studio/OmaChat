#include "audio/PipeWireBackend.hpp"

#include "omachat/core/Log.hpp"

#include <QMetaObject>
#include <QPointer>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/dict.h>

#include <cstring>

namespace omachat::audio {

struct PipeWireBackend::Impl {
    PipeWireBackend* owner = nullptr;
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook registryListener{};
    spa_hook coreListener{};

    pw_stream* capture = nullptr;
    pw_stream* playback = nullptr;
    spa_hook captureListener{};
    spa_hook playbackListener{};
    CaptureFn captureFn;
    PlaybackFn playbackFn;

    mutable std::mutex devicesMutex;
    std::map<std::uint32_t, AudioDevice> devices;
    bool connected = false;

    // Marshal a notification to the Qt thread.
    void post(auto fn)
    {
        QPointer<PipeWireBackend> guard(owner);
        QMetaObject::invokeMethod(
            owner,
            [guard, fn = std::move(fn)] {
                if (guard)
                    fn(guard.data());
            },
            Qt::QueuedConnection);
    }

    // ---- registry
    static void onGlobal(
        void* data, std::uint32_t id, std::uint32_t, const char* type, std::uint32_t, const spa_dict* props)
    {
        auto* self = static_cast<Impl*>(data);
        if (!props || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
            return;
        const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        if (!mediaClass)
            return;
        const bool source
            = std::strcmp(mediaClass, "Audio/Source") == 0 || std::strcmp(mediaClass, "Audio/Source/Virtual") == 0;
        const bool sink = std::strcmp(mediaClass, "Audio/Sink") == 0;
        if (!source && !sink)
            return;
        const char* nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        if (!nodeName)
            return;
        const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        if (!desc)
            desc = spa_dict_lookup(props, PW_KEY_NODE_NICK);
        AudioDevice dev{QString::fromUtf8(nodeName), QString::fromUtf8(desc ? desc : nodeName), source};
        {
            std::lock_guard lock(self->devicesMutex);
            self->devices[id] = dev;
        }
        self->post([](PipeWireBackend* b) { emit b->devicesChanged(); });
    }

    static void onGlobalRemove(void* data, std::uint32_t id)
    {
        auto* self = static_cast<Impl*>(data);
        bool removed = false;
        {
            std::lock_guard lock(self->devicesMutex);
            removed = self->devices.erase(id) > 0;
        }
        if (removed)
            self->post([](PipeWireBackend* b) { emit b->devicesChanged(); });
    }

    static void onCoreError(void* data, std::uint32_t id, int, int res, const char* message)
    {
        auto* self = static_cast<Impl*>(data);
        if (id != PW_ID_CORE || res != -EPIPE)
            return;
        // The PipeWire daemon went away.
        const QString reason = QString::fromUtf8(message ? message : "PipeWire connection lost");
        self->post([reason](PipeWireBackend* b) { emit b->streamFailed(reason); });
    }

    // ---- streams (realtime thread)
    static void onCaptureProcess(void* data)
    {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->capture);
        if (!b)
            return;
        spa_data& d = b->buffer->datas[0];
        if (d.data && d.chunk && self->captureFn) {
            const auto* samples = static_cast<const float*>(SPA_PTROFF(d.data, d.chunk->offset, void));
            self->captureFn(samples, d.chunk->size / sizeof(float));
        }
        pw_stream_queue_buffer(self->capture, b);
    }

    static void onPlaybackProcess(void* data)
    {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->playback);
        if (!b)
            return;
        spa_data& d = b->buffer->datas[0];
        if (!d.data) {
            pw_stream_queue_buffer(self->playback, b);
            return;
        }
        auto* out = static_cast<float*>(d.data);
        std::uint32_t frames = d.maxsize / sizeof(float);
        if (b->requested)
            frames = std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(b->requested));
        if (self->playbackFn)
            self->playbackFn(out, frames);
        else
            std::memset(out, 0, frames * sizeof(float));
        d.chunk->offset = 0;
        d.chunk->stride = sizeof(float);
        d.chunk->size = frames * sizeof(float);
        pw_stream_queue_buffer(self->playback, b);
    }

    static void onStateChanged(void* data, pw_stream_state, pw_stream_state state, const char* error)
    {
        auto* self = static_cast<Impl*>(data);
        if (state == PW_STREAM_STATE_ERROR) {
            const QString reason = QString::fromUtf8(error ? error : "audio stream error");
            self->post([reason](PipeWireBackend* b) { emit b->streamFailed(reason); });
        }
    }

    pw_stream* makeStream(bool isCapture, const QString& target, spa_hook* listener, const pw_stream_events* events)
    {
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
            isCapture ? "Capture" : "Playback", PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_APP_NAME, "OmaChat",
            PW_KEY_APP_ID, "org.omarchy.OmaChat", PW_KEY_NODE_LATENCY, "480/48000", nullptr);
        if (!target.isEmpty() && target != u"default")
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.toUtf8().constData());
        pw_stream* s = pw_stream_new(core, isCapture ? "OmaChat Microphone" : "OmaChat Voice", props);
        if (!s)
            return nullptr;
        pw_stream_add_listener(s, listener, events, this);

        std::uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof buffer);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = 48000;
        info.channels = 1;
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        const spa_pod* params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        const auto flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
        if (pw_stream_connect(s, isCapture ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1)
            < 0) {
            pw_stream_destroy(s);
            return nullptr;
        }
        return s;
    }
};

namespace {

const pw_registry_events kRegistryEvents = [] {
    pw_registry_events e{};
    e.version = PW_VERSION_REGISTRY_EVENTS;
    e.global = &PipeWireBackend::Impl::onGlobal;
    e.global_remove = &PipeWireBackend::Impl::onGlobalRemove;
    return e;
}();

const pw_core_events kCoreEvents = [] {
    pw_core_events e{};
    e.version = PW_VERSION_CORE_EVENTS;
    e.error = &PipeWireBackend::Impl::onCoreError;
    return e;
}();

const pw_stream_events kCaptureEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.state_changed = &PipeWireBackend::Impl::onStateChanged;
    e.process = &PipeWireBackend::Impl::onCaptureProcess;
    return e;
}();

const pw_stream_events kPlaybackEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.state_changed = &PipeWireBackend::Impl::onStateChanged;
    e.process = &PipeWireBackend::Impl::onPlaybackProcess;
    return e;
}();

} // namespace

PipeWireBackend::PipeWireBackend(QObject* parent)
    : AudioBackend(parent)
    , d(std::make_unique<Impl>())
{
    d->owner = this;
    pw_init(nullptr, nullptr);
    d->loop = pw_thread_loop_new("omachat-pw", nullptr);
    if (!d->loop)
        return;
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context || pw_thread_loop_start(d->loop) < 0)
        return;

    pw_thread_loop_lock(d->loop);
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (d->core) {
        pw_core_add_listener(d->core, &d->coreListener, &kCoreEvents, d.get());
        d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(d->registry, &d->registryListener, &kRegistryEvents, d.get());
        d->connected = true;
    }
    pw_thread_loop_unlock(d->loop);
    if (!d->connected)
        OMA_WARN("audio", "cannot connect to PipeWire");
}

PipeWireBackend::~PipeWireBackend()
{
    stopStreams();
    if (d->loop)
        pw_thread_loop_stop(d->loop);
    if (d->registry) {
        spa_hook_remove(&d->registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(d->registry));
    }
    if (d->core) {
        spa_hook_remove(&d->coreListener);
        pw_core_disconnect(d->core);
    }
    if (d->context)
        pw_context_destroy(d->context);
    if (d->loop)
        pw_thread_loop_destroy(d->loop);
    pw_deinit();
}

bool PipeWireBackend::available() const
{
    return d->connected;
}

QList<AudioDevice> PipeWireBackend::devices() const
{
    std::lock_guard lock(d->devicesMutex);
    QList<AudioDevice> out;
    for (const auto& [id, dev] : d->devices)
        out.append(dev);
    return out;
}

bool PipeWireBackend::startStreams(
    const QString& input, const QString& output, CaptureFn capture, PlaybackFn playback, QString* error)
{
    if (!d->connected) {
        if (error)
            *error = QStringLiteral("PipeWire is not available");
        return false;
    }
    stopStreams();
    pw_thread_loop_lock(d->loop);
    d->captureFn = std::move(capture);
    d->playbackFn = std::move(playback);
    d->capture = d->makeStream(true, input, &d->captureListener, &kCaptureEvents);
    d->playback = d->makeStream(false, output, &d->playbackListener, &kPlaybackEvents);
    const bool ok = d->capture && d->playback;
    pw_thread_loop_unlock(d->loop);
    if (!ok) {
        stopStreams();
        if (error)
            *error = QStringLiteral("could not open PipeWire audio streams");
        return false;
    }
    OMA_INFO("audio", "streams started", {"input", input}, {"output", output});
    return true;
}

void PipeWireBackend::stopStreams()
{
    if (!d->loop)
        return;
    pw_thread_loop_lock(d->loop);
    if (d->capture) {
        spa_hook_remove(&d->captureListener);
        pw_stream_destroy(d->capture);
        d->capture = nullptr;
    }
    if (d->playback) {
        spa_hook_remove(&d->playbackListener);
        pw_stream_destroy(d->playback);
        d->playback = nullptr;
    }
    d->captureFn = {};
    d->playbackFn = {};
    pw_thread_loop_unlock(d->loop);
}

bool PipeWireBackend::streaming() const
{
    return d->capture != nullptr;
}

} // namespace omachat::audio
