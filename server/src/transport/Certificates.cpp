#include "transport/Certificates.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstdio>
#include <memory>

#include <sys/stat.h>

namespace omachat::server {
namespace {

template <typename T, void (*Free)(T*)> struct Deleter {
    void operator()(T* p) const { Free(p); }
};
using PKeyPtr = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using X509Ptr = std::unique_ptr<X509, Deleter<X509, X509_free>>;
using ExtPtr = std::unique_ptr<X509_EXTENSION, Deleter<X509_EXTENSION, X509_EXTENSION_free>>;

struct FileCloser {
    void operator()(FILE* f) const { std::fclose(f); }
};

} // namespace

bool loadTlsIdentity(const QString& certPath, const QString& keyPath, TlsIdentity& out, QString* error)
{
    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read certificate %1: %2").arg(certPath, certFile.errorString());
        return false;
    }
    out.chain = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
    if (out.chain.isEmpty()) {
        if (error)
            *error = QStringLiteral("no PEM certificate found in %1").arg(certPath);
        return false;
    }
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read private key %1: %2").arg(keyPath, keyFile.errorString());
        return false;
    }
    const QByteArray keyData = keyFile.readAll();
    for (QSsl::KeyAlgorithm alg : {QSsl::Ec, QSsl::Rsa, QSsl::Opaque}) {
        QSslKey k(keyData, alg, QSsl::Pem, QSsl::PrivateKey);
        if (!k.isNull()) {
            out.key = k;
            break;
        }
    }
    if (out.key.isNull()) {
        if (error)
            *error = QStringLiteral("unsupported or encrypted private key in %1").arg(keyPath);
        return false;
    }
    return true;
}

bool generateSelfSigned(
    const QString& certPath, const QString& keyPath, const QStringList& names, int days, QString* error)
{
    auto fail = [&](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (QFileInfo::exists(certPath) || QFileInfo::exists(keyPath))
        return fail(QStringLiteral("refusing to overwrite existing certificate or key"));

    PKeyPtr key(EVP_EC_gen("P-256"));
    if (!key)
        return fail(QStringLiteral("EC key generation failed"));

    X509Ptr cert(X509_new());
    X509_set_version(cert.get(), 2);
    unsigned char serial[16];
    RAND_bytes(serial, sizeof serial);
    serial[0] &= 0x7f;
    BIGNUM* bn = BN_bin2bn(serial, sizeof serial, nullptr);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert.get()));
    BN_free(bn);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), -300);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), static_cast<long>(days) * 24 * 3600);
    X509_set_pubkey(cert.get(), key.get());

    const QByteArray cn = (names.isEmpty() ? QStringLiteral("omachat-server") : names.first()).toUtf8();
    X509_NAME* subject = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(
        subject, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(cn.constData()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(
        subject, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("OmaChat"), -1, -1, 0);
    X509_set_issuer_name(cert.get(), subject);

    QStringList san;
    for (const QString& n : names) {
        QHostAddress addr;
        san << (addr.setAddress(n) ? QStringLiteral("IP:") : QStringLiteral("DNS:")) + n;
    }
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert.get(), cert.get(), nullptr, nullptr, 0);
    auto addExt = [&](int nid, const QByteArray& value) {
        ExtPtr ext(X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.constData()));
        return ext && X509_add_ext(cert.get(), ext.get(), -1) == 1;
    };
    if (!san.isEmpty() && !addExt(NID_subject_alt_name, san.join(u',').toUtf8()))
        return fail(QStringLiteral("invalid subjectAltName entries"));
    addExt(NID_basic_constraints, "critical,CA:FALSE");
    addExt(NID_key_usage, "critical,digitalSignature");
    addExt(NID_ext_key_usage, "serverAuth");

    if (X509_sign(cert.get(), key.get(), EVP_sha256()) == 0)
        return fail(QStringLiteral("certificate signing failed"));

    // Create the key file with 0600 before writing any key material.
    const QByteArray keyName = QFile::encodeName(keyPath);
    const mode_t oldMask = ::umask(077);
    std::unique_ptr<FILE, FileCloser> kf(std::fopen(keyName.constData(), "wx"));
    ::umask(oldMask);
    if (!kf)
        return fail(QStringLiteral("cannot create %1").arg(keyPath));
    if (PEM_write_PrivateKey(kf.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
        return fail(QStringLiteral("cannot write private key"));
    kf.reset();

    std::unique_ptr<FILE, FileCloser> cf(std::fopen(QFile::encodeName(certPath).constData(), "wx"));
    if (!cf)
        return fail(QStringLiteral("cannot create %1").arg(certPath));
    if (PEM_write_X509(cf.get(), cert.get()) != 1)
        return fail(QStringLiteral("cannot write certificate"));
    return true;
}

QString fingerprint(const QSslCertificate& cert)
{
    const QByteArray digest = cert.digest(QCryptographicHash::Sha256).toHex().toUpper();
    QStringList parts;
    for (qsizetype i = 0; i < digest.size(); i += 2)
        parts << QString::fromLatin1(digest.mid(i, 2));
    return QStringLiteral("SHA256:") + parts.join(u':');
}

} // namespace omachat::server
