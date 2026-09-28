#pragma once

#include <QSslCertificate>
#include <QSslKey>
#include <QString>
#include <QStringList>

namespace omachat::server {

struct TlsIdentity {
    QList<QSslCertificate> chain;
    QSslKey key;
};

// Loads a PEM certificate chain and private key.
bool loadTlsIdentity(const QString& certPath, const QString& keyPath, TlsIdentity& out, QString* error);

// Generates a self-signed ECDSA P-256 certificate valid for `days`, with the
// given DNS names / IP addresses as subjectAltName. The key is written with
// mode 0600. Existing files are never overwritten.
bool generateSelfSigned(
    const QString& certPath, const QString& keyPath, const QStringList& names, int days, QString* error);

// "SHA256:AB:CD:..." fingerprint of the DER certificate.
QString fingerprint(const QSslCertificate& cert);

} // namespace omachat::server
