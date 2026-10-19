#pragma once
#include <QTcpServer>
#include <functional>
namespace omachat::server {
// One bounded HTTP/1.1 request per connection. No chunking, pipelining or upgrades.
class WebhookIngress : public QTcpServer {
public:
    using Handler = std::function<int(const QString&, const QByteArray&)>;
    explicit WebhookIngress(QObject* parent = nullptr);
    Handler handler;

private:
    int m_active = 0;
};
} // namespace omachat::server
