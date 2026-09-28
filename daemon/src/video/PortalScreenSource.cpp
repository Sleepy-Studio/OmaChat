// Screen capture through xdg-desktop-portal's ScreenCast interface:
//
//   CreateSession -> SelectSources (monitor or window) -> Start (the desktop
//   shows its picker) -> OpenPipeWireRemote -> a PipeWire video stream on
//   that remote, negotiated to shared-memory buffers only.
//
// Every portal call answers through a Request object's Response signal; we
// subscribe to the predicted request path before calling so no answer is
// missed.

#include "omachat/core/Log.hpp"
#include "video/ScreenSource.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QPointer>
#include <QRandomGenerator>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>

#include <unistd.h>

namespace omachat::video {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenCast = QStringLiteral("org.freedesktop.portal.ScreenCast");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kSession = QStringLiteral("org.freedesktop.portal.Session");

constexpr std::uint32_t kSourceMonitor = 1;
constexpr std::uint32_t kSourceWindow = 2;
constexpr std::uint32_t kCursorEmbedded = 2;
constexpr std::uint32_t kCursorHidden = 1;

std::optional<PixelFormat> fromSpa(std::uint32_t f)
{
    switch (f) {
    case SPA_VIDEO_FORMAT_BGRx:
        return PixelFormat::BGRx;
    case SPA_VIDEO_FORMAT_BGRA:
        return PixelFormat::BGRA;
    case SPA_VIDEO_FORMAT_RGBx:
        return PixelFormat::RGBx;
    case SPA_VIDEO_FORMAT_RGBA:
        return PixelFormat::RGBA;
    case SPA_VIDEO_FORMAT_xRGB:
        return PixelFormat::xRGB;
    case SPA_VIDEO_FORMAT_xBGR:
        return PixelFormat::xBGR;
    default:
        return std::nullopt;
    }
}

} // namespace

class PortalScreenSource : public ScreenSource {
    Q_OBJECT
public:
    explicit PortalScreenSource(QObject* parent)
        : ScreenSource(parent)
        , m_bus(QDBusConnection::sessionBus())
    {
        pw_init(nullptr, nullptr);
    }

    ~PortalScreenSource() override { stop(); }

    QString name() const override { return QStringLiteral("portal"); }

    void start(FrameFn onFrame) override
    {
        stop();
        m_onFrame = std::move(onFrame);
        if (!m_bus.isConnected()) {
            fail(QStringLiteral("no D-Bus session bus"));
            return;
        }
        const QString sessionToken = token();
        call(QStringLiteral("CreateSession"), {QVariantMap{{"session_handle_token", sessionToken}}},
            [this](const QVariantMap& results) {
                m_session = results.value(QStringLiteral("session_handle")).toString();
                if (m_session.isEmpty()) {
                    fail(QStringLiteral("the screen-sharing portal returned no session"));
                    return;
                }
                m_bus.connect(kService, m_session, kSession, QStringLiteral("Closed"), this, SLOT(onSessionClosed()));
                selectSources();
            });
    }

    void stop() override
    {
        stopStream();
        if (!m_session.isEmpty()) {
            m_bus.disconnect(kService, m_session, kSession, QStringLiteral("Closed"), this, SLOT(onSessionClosed()));
            QDBusMessage close = QDBusMessage::createMethodCall(kService, m_session, kSession, QStringLiteral("Close"));
            m_bus.asyncCall(close);
            m_session.clear();
        }
        m_pending.clear();
        m_started = false;
    }

private slots:
    void onResponse(uint response, const QVariantMap& results)
    {
        // Requests are strictly sequential, so the one waiting is the one answering.
        if (m_pending.isEmpty())
            return;
        const auto [path, next] = m_pending.takeFirst();
        m_bus.disconnect(
            kService, path, kRequest, QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
        if (response == 1) {
            fail(QStringLiteral("cancelled"));
            return;
        }
        if (response != 0) {
            fail(QStringLiteral("the screen-sharing portal refused (code %1)").arg(response));
            return;
        }
        next(results);
    }

    void onSessionClosed()
    {
        OMA_INFO("video", "screen-sharing session closed by the desktop");
        const bool wasStarted = m_started;
        m_session.clear();
        stop();
        if (wasStarted)
            emit ended();
    }

private:
    using Next = std::function<void(const QVariantMap&)>;

    QString token() const
    {
        return QStringLiteral("omachat_%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    }

    // Calls a ScreenCast method whose last argument is the options map and
    // routes its Response to `next`.
    void call(const QString& method, QVariantList args, Next next)
    {
        const QString handleToken = token();
        QVariantMap options = args.takeLast().toMap();
        options.insert(QStringLiteral("handle_token"), handleToken);
        args.append(options);
        QString sender = m_bus.baseService().mid(1);
        sender.replace(u'.', u'_');
        const QString requestPath = QStringLiteral("%1/request/%2/%3").arg(kPath, sender, handleToken);
        m_bus.connect(
            kService, requestPath, kRequest, QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
        m_pending.append({requestPath, std::move(next)});

        QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kScreenCast, method);
        msg.setArguments(args);
        auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (w->isError())
                fail(QStringLiteral("screen-sharing portal: %1 failed: %2").arg(method, w->error().message()));
        });
    }

    void selectSources()
    {
        QDBusInterface props(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), m_bus);
        const QDBusReply<QDBusVariant> cursor
            = props.call(QStringLiteral("Get"), kScreenCast, QStringLiteral("AvailableCursorModes"));
        const std::uint32_t modes = cursor.isValid() ? cursor.value().variant().toUInt() : 0;
        const std::uint32_t cursorMode = (modes & kCursorEmbedded) ? kCursorEmbedded : kCursorHidden;
        QVariantMap options{
            {"types", kSourceMonitor | kSourceWindow}, {"multiple", false}, {"cursor_mode", cursorMode}};
        call(QStringLiteral("SelectSources"), {QVariant::fromValue(QDBusObjectPath(m_session)), options},
            [this](const QVariantMap&) { startSession(); });
    }

    void startSession()
    {
        call(QStringLiteral("Start"), {QVariant::fromValue(QDBusObjectPath(m_session)), QString(), QVariantMap{}},
            [this](const QVariantMap& results) {
                std::uint32_t node = 0;
                const QDBusArgument streams = results.value(QStringLiteral("streams")).value<QDBusArgument>();
                streams.beginArray();
                while (!streams.atEnd()) {
                    std::uint32_t id = 0;
                    QVariantMap properties;
                    streams.beginStructure();
                    streams >> id >> properties;
                    streams.endStructure();
                    if (!node)
                        node = id;
                }
                streams.endArray();
                if (!node) {
                    fail(QStringLiteral("nothing was selected to share"));
                    return;
                }
                openRemote(node);
            });
    }

    void openRemote(std::uint32_t node)
    {
        QDBusMessage msg
            = QDBusMessage::createMethodCall(kService, kPath, kScreenCast, QStringLiteral("OpenPipeWireRemote"));
        msg.setArguments({QVariant::fromValue(QDBusObjectPath(m_session)), QVariantMap{}});
        const QDBusReply<QDBusUnixFileDescriptor> reply = m_bus.call(msg);
        if (!reply.isValid() || !reply.value().isValid()) {
            fail(QStringLiteral("could not open the screen stream: %1").arg(reply.error().message()));
            return;
        }
        const int fd = ::dup(reply.value().fileDescriptor());
        QString error;
        if (fd < 0 || !startStream(fd, node, &error)) {
            fail(error.isEmpty() ? QStringLiteral("could not open the screen stream") : error);
            return;
        }
        m_started = true;
        OMA_INFO("video", "screen capture started", {"node", node});
        emit started();
    }

    void fail(const QString& reason)
    {
        const bool wasStarted = m_started;
        OMA_INFO("video", "screen capture unavailable", {"reason", reason});
        stop();
        if (wasStarted)
            emit ended();
        else
            emit failed(reason);
    }

    // ---- PipeWire (fd from the portal; takes ownership)
    bool startStream(int fd, std::uint32_t node, QString* error)
    {
        m_loop = pw_thread_loop_new("omachat-screen", nullptr);
        m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
        if (!m_loop || !m_context || pw_thread_loop_start(m_loop) < 0) {
            ::close(fd);
            *error = QStringLiteral("PipeWire is not available");
            stopStream();
            return false;
        }
        pw_thread_loop_lock(m_loop);
        m_core = pw_context_connect_fd(m_context, fd, nullptr, 0);
        if (!m_core) {
            pw_thread_loop_unlock(m_loop);
            *error = QStringLiteral("cannot connect to the screen stream");
            stopStream();
            return false;
        }
        m_stream = pw_stream_new(m_core, "OmaChat screen share",
            pw_properties_new(
                PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
        static const pw_stream_events events = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.param_changed = &PortalScreenSource::onParamChanged;
            e.process = &PortalScreenSource::onProcess;
            e.state_changed = &PortalScreenSource::onStateChanged;
            return e;
        }();
        pw_stream_add_listener(m_stream, &m_streamListener, &events, this);

        std::uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        spa_rectangle defSize{1920, 1080}, minSize{1, 1}, maxSize{8192, 8192};
        spa_fraction defRate{30, 1}, minRate{0, 1}, maxRate{144, 1};
        const spa_pod* params[1];
        params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_Format,
            SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
            SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
            SPA_POD_CHOICE_ENUM_Id(7, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
                SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_xRGB, SPA_VIDEO_FORMAT_xBGR),
            SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate)));
        const int rc = pw_stream_connect(m_stream, PW_DIRECTION_INPUT, node,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
        pw_thread_loop_unlock(m_loop);
        if (rc < 0) {
            *error = QStringLiteral("cannot connect to the screen stream");
            stopStream();
            return false;
        }
        return true;
    }

    void stopStream()
    {
        if (m_loop)
            pw_thread_loop_stop(m_loop);
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
        m_format.reset();
    }

    static void onParamChanged(void* data, std::uint32_t id, const spa_pod* param)
    {
        auto* self = static_cast<PortalScreenSource*>(data);
        if (!param || id != SPA_PARAM_Format)
            return;
        spa_video_info_raw info{};
        if (spa_format_video_raw_parse(param, &info) < 0)
            return;
        self->m_format = fromSpa(info.format);
        self->m_width = static_cast<int>(info.size.width);
        self->m_height = static_cast<int>(info.size.height);
        // Shared memory only: DMA-BUF would need GPU import on our side.
        std::uint8_t buffer[256];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod* params[1];
        params[0] = static_cast<const spa_pod*>(
            spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_dataType,
                SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))));
        pw_stream_update_params(self->m_stream, params, 1);
    }

    static void onProcess(void* data)
    {
        auto* self = static_cast<PortalScreenSource*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->m_stream);
        if (!b)
            return;
        const spa_data& d = b->buffer->datas[0];
        if (d.data && d.chunk && d.chunk->size > 0 && !(d.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) && self->m_format
            && self->m_onFrame) {
            const int stride = d.chunk->stride > 0 ? d.chunk->stride : self->m_width * 4;
            if (static_cast<std::size_t>(stride) * self->m_height <= d.chunk->size + d.chunk->offset) {
                self->m_onFrame(Frame{static_cast<const std::uint8_t*>(SPA_PTROFF(d.data, d.chunk->offset, void)),
                    self->m_width, self->m_height, stride, *self->m_format});
            }
        }
        pw_stream_queue_buffer(self->m_stream, b);
    }

    static void onStateChanged(void* data, pw_stream_state, pw_stream_state state, const char* error)
    {
        auto* self = static_cast<PortalScreenSource*>(data);
        if (state != PW_STREAM_STATE_ERROR && state != PW_STREAM_STATE_UNCONNECTED)
            return;
        const QString reason = QString::fromUtf8(error ? error : "the screen stream ended");
        QPointer<PortalScreenSource> guard(self);
        QMetaObject::invokeMethod(
            self,
            [guard, reason] {
                if (guard && guard->m_started)
                    guard->fail(reason);
            },
            Qt::QueuedConnection);
    }

    QDBusConnection m_bus;
    QString m_session;
    QList<QPair<QString, Next>> m_pending;
    FrameFn m_onFrame;
    bool m_started = false;

    pw_thread_loop* m_loop = nullptr;
    pw_context* m_context = nullptr;
    pw_core* m_core = nullptr;
    pw_stream* m_stream = nullptr;
    spa_hook m_streamListener{};
    std::optional<PixelFormat> m_format;
    int m_width = 0;
    int m_height = 0;
};

std::unique_ptr<ScreenSource> makePortalSource(QObject* parent)
{
    return std::make_unique<PortalScreenSource>(parent);
}

} // namespace omachat::video

#include "PortalScreenSource.moc"
