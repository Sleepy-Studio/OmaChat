#include "omachat/media/FrameBuffer.hpp"

#include <QFile>

#include <atomic>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace omachat::media {

namespace {

constexpr char kMagic[8] = {'O', 'M', 'A', 'F', 'R', 'A', 'M', 'E'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kCapacity = std::size_t{kMaxFrameWidth} * kMaxFrameHeight * 4;

struct Layout {
    char magic[8];
    std::uint32_t version;
    std::uint32_t capacity;
    std::uint64_t sequence;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    std::uint32_t reserved;
    std::int64_t timestampMs;
};
static_assert(sizeof(Layout) <= kFrameHeaderBytes);
static_assert(offsetof(Layout, sequence) == 16);

std::atomic_ref<std::uint64_t> sequenceOf(std::uint8_t* map)
{
    return std::atomic_ref<std::uint64_t>(reinterpret_cast<Layout*>(map)->sequence);
}

} // namespace

FrameBufferWriter::~FrameBufferWriter()
{
    close();
}

bool FrameBufferWriter::create(const QString& path, QString* error)
{
    close();
    const QByteArray native = QFile::encodeName(path);
    ::unlink(native.constData());
    const int fd = ::open(native.constData(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    const std::size_t size = kFrameHeaderBytes + kCapacity;
    // Sparse until frames are written; small frames never touch the rest.
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        ::close(fd);
        ::unlink(native.constData());
        return false;
    }
    void* map = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        ::unlink(native.constData());
        return false;
    }
    m_map = static_cast<std::uint8_t*>(map);
    m_size = size;
    m_path = path;
    auto* l = reinterpret_cast<Layout*>(m_map);
    std::memcpy(l->magic, kMagic, sizeof(kMagic));
    l->version = kVersion;
    l->capacity = static_cast<std::uint32_t>(kCapacity);
    return true;
}

void FrameBufferWriter::close()
{
    if (m_map) {
        ::munmap(m_map, m_size);
        m_map = nullptr;
        m_size = 0;
    }
    if (!m_path.isEmpty()) {
        ::unlink(QFile::encodeName(m_path).constData());
        m_path.clear();
    }
}

bool FrameBufferWriter::write(
    const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride)
{
    if (!m_map || width == 0 || height == 0 || width > kMaxFrameWidth || height > kMaxFrameHeight || stride < width * 4)
        return false;
    auto* l = reinterpret_cast<Layout*>(m_map);
    auto seq = sequenceOf(m_map);
    const std::uint64_t start = seq.load(std::memory_order_relaxed);
    seq.store(start + 1, std::memory_order_relaxed); // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    l->width = width;
    l->height = height;
    l->stride = width * 4;
    auto* dst = m_map + kFrameHeaderBytes;
    for (std::uint32_t y = 0; y < height; ++y)
        std::memcpy(dst + std::size_t{y} * width * 4, pixels + std::size_t{y} * stride, std::size_t{width} * 4);
    l->timestampMs
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
    seq.store(start + 2, std::memory_order_release); // even: complete
    return true;
}

FrameBufferReader::~FrameBufferReader()
{
    close();
}

bool FrameBufferReader::open(const QString& path)
{
    close();
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st{};
    if (::fstat(fd, &st) != 0 || static_cast<std::size_t>(st.st_size) < kFrameHeaderBytes) {
        ::close(fd);
        return false;
    }
    void* map = ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED)
        return false;
    const auto* l = static_cast<const Layout*>(map);
    if (std::memcmp(l->magic, kMagic, sizeof(kMagic)) != 0 || l->version != kVersion
        || kFrameHeaderBytes + l->capacity > static_cast<std::size_t>(st.st_size)) {
        ::munmap(map, static_cast<std::size_t>(st.st_size));
        return false;
    }
    m_map = static_cast<const std::uint8_t*>(map);
    m_size = static_cast<std::size_t>(st.st_size);
    return true;
}

void FrameBufferReader::close()
{
    if (m_map) {
        ::munmap(const_cast<std::uint8_t*>(m_map), m_size);
        m_map = nullptr;
        m_size = 0;
    }
}

bool FrameBufferReader::readIfNewer(std::uint64_t lastSequence, Frame& out) const
{
    if (!m_map)
        return false;
    auto* raw = const_cast<std::uint8_t*>(m_map); // atomic_ref needs non-const; only loads happen
    auto seq = sequenceOf(raw);
    const std::uint64_t before = seq.load(std::memory_order_acquire);
    if (before == 0 || (before & 1) || before == lastSequence)
        return false;
    const auto* l = reinterpret_cast<const Layout*>(m_map);
    const std::uint32_t w = l->width, h = l->height;
    if (w == 0 || h == 0 || w > kMaxFrameWidth || h > kMaxFrameHeight
        || std::size_t{w} * h * 4 > m_size - kFrameHeaderBytes)
        return false;
    out.pixels.resize(std::size_t{w} * h * 4);
    std::memcpy(out.pixels.data(), m_map + kFrameHeaderBytes, out.pixels.size());
    std::atomic_thread_fence(std::memory_order_acquire);
    if (seq.load(std::memory_order_relaxed) != before)
        return false; // overwritten while copying; the next poll gets it
    out.width = w;
    out.height = h;
    out.sequence = before;
    return true;
}

} // namespace omachat::media
