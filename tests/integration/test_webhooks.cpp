#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpSocket>
#include <gtest/gtest.h>
using namespace omachat;
using namespace omachat::test;
namespace {
class Webhooks : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> owner;
    proto::IncomingWebhook hook;
    std::uint64_t channel = 0;
    std::uint64_t community = 0;
    void SetUp() override
    {
        server.setWebhooksEnabled(true);
        ASSERT_TRUE(server.start());
        owner = std::make_unique<RawClient>(server);
        ASSERT_TRUE(owner->connect());
        ASSERT_TRUE(owner->hello());
        ASSERT_TRUE(owner->registerUser("webhook-owner", "webhook-password"));
        proto::Envelope req;
        req.mutable_create_server()->set_name("Webhook tests");
        auto res = owner->call(req);
        ASSERT_TRUE(res && res->has_server());
        community = res->server().id();
        req.Clear();
        req.mutable_sync();
        res = owner->call(req);
        ASSERT_TRUE(res && res->has_sync_state());
        for (const auto& c : res->sync_state().channels())
            if (c.type() == proto::CHANNEL_TYPE_TEXT) {
                channel = c.id();
                break;
            }
        ASSERT_NE(channel, 0u);
        req.Clear();
        req.mutable_create_webhook()->set_channel_id(channel);
        req.mutable_create_webhook()->set_name("CI bot");
        res = owner->call(req);
        ASSERT_TRUE(res && res->has_incoming_webhook());
        hook = res->incoming_webhook();
    }
    QByteArray request(const QByteArray& token, const QByteArray& body, const QByteArray& extra = {})
    {
        return "POST /hooks/" + token
            + " HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\n" + extra + "\r\n" + body;
    }
    QByteArray send(const QByteArray& bytes)
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.server()->webhookPort());
        if (!waitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }))
            return {};
        socket.write(bytes);
        QByteArray response;
        waitFor([&] {
            response += socket.readAll();
            return socket.state() == QAbstractSocket::UnconnectedState;
        });
        response += socket.readAll();
        return response;
    }
};
TEST_F(Webhooks, DeliveryAttributionHistoryAndRevocation)
{
    auto token = QByteArray::fromStdString(hook.token());
    ASSERT_EQ(token.size(), 43);
    ASSERT_TRUE(send(request(token, R"({"content":"Build failed @everyone"})")).startsWith("HTTP/1.1 201"));
    proto::Envelope req;
    req.mutable_get_messages()->set_channel_id(channel);
    auto res = owner->call(req);
    ASSERT_TRUE(res && res->has_message_page());
    ASSERT_EQ(res->message_page().messages_size(), 1);
    const auto& message = res->message_page().messages(0);
    EXPECT_EQ(message.integration_id(), hook.id());
    EXPECT_EQ(message.integration_name(), "CI bot");
    EXPECT_TRUE(message.content().starts_with("[Integration: CI bot]"));
    EXPECT_EQ(message.mention_ids_size(), 0);
    EXPECT_TRUE(message.mention_classification().classified());
    EXPECT_EQ(message.mention_classification().targets_size(), 0);
    EXPECT_EQ(message.mention_classification().reason_mask(), 0u);
    req.Clear();
    req.mutable_get_unread_summary()->set_channel_id(channel);
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_unread_summary());
    EXPECT_EQ(res->unread_summary().count(), 1u);
    EXPECT_EQ(res->unread_summary().mentions(), 0u);
    req.Clear();
    req.mutable_get_attention_summary()->set_channel_id(channel);
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_attention_summary());
    EXPECT_EQ(res->attention_summary().unread().count(), 1u);
    EXPECT_TRUE(res->attention_summary().classification_complete());
    for (auto count : res->attention_summary().mask_counts())
        EXPECT_EQ(count, 0u);

    req.Clear();
    req.mutable_list_webhooks()->set_channel_id(channel);
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_webhook_list());
    ASSERT_EQ(res->webhook_list().webhooks_size(), 1);
    EXPECT_TRUE(res->webhook_list().webhooks(0).token().empty());
    req.Clear();
    req.mutable_revoke_webhook()->set_channel_id(channel);
    req.mutable_revoke_webhook()->set_webhook_id(hook.id());
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_ok());
    EXPECT_TRUE(send(request(token, R"({"content":"revoked"})")).startsWith("HTTP/1.1 401"));
}
TEST_F(Webhooks, StrictFramingPayloadAndRateLimits)
{
    auto token = QByteArray::fromStdString(hook.token());
    EXPECT_TRUE(send(request(token, R"({"content":"hi"})", "Content-Length: 16\r\n")).startsWith("HTTP/1.1 400"));
    EXPECT_TRUE(
        send(request(token, R"({"content":"hi"})", "Transfer-Encoding: chunked\r\n")).startsWith("HTTP/1.1 400"));
    EXPECT_TRUE(send(request(token, QByteArray(16385, 'x'))).startsWith("HTTP/1.1 413"));
    EXPECT_TRUE(send(request(token, R"({"content":"hi","username":"spoof"})")).startsWith("HTTP/1.1 400"));
    EXPECT_TRUE(send(request(token, R"({"content":""})")).startsWith("HTTP/1.1 400"));
    int accepted = 0, limited = 0;
    for (int i = 0; i < 8; ++i) {
        const auto response = send(request(token, R"({"content":"event"})"));
        accepted += response.startsWith("HTTP/1.1 201");
        limited += response.startsWith("HTTP/1.1 429");
    }
    EXPECT_GT(accepted, 0);
    EXPECT_GT(limited, 0);
}
TEST_F(Webhooks, NonManagersCannotCreateListOrRevoke)
{
    RawClient other(server);
    ASSERT_TRUE(other.connect());
    ASSERT_TRUE(other.hello());
    ASSERT_TRUE(other.registerUser("webhook-other", "webhook-password"));
    proto::Envelope req;
    req.mutable_create_webhook()->set_channel_id(channel);
    req.mutable_create_webhook()->set_name("spoof");
    auto res = other.call(req);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->error().code(), proto::ERROR_PERMISSION_DENIED);
    req.Clear();
    req.mutable_list_webhooks()->set_channel_id(channel);
    res = other.call(req);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->error().code(), proto::ERROR_PERMISSION_DENIED);
    req.Clear();
    req.mutable_revoke_webhook()->set_channel_id(channel);
    req.mutable_revoke_webhook()->set_webhook_id(hook.id());
    res = other.call(req);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->error().code(), proto::ERROR_PERMISSION_DENIED);
}

TEST_F(Webhooks, CurrentChannelPermissionsAndRevocationScopeAreEnforced)
{
    RawClient manager(server);
    ASSERT_TRUE(manager.connect());
    ASSERT_TRUE(manager.hello());
    const auto auth = manager.registerUser("webhook-manager", "webhook-password");
    ASSERT_TRUE(auth);
    proto::Envelope req;
    req.mutable_create_invite()->set_server_id(community);
    auto res = owner->call(req);
    ASSERT_TRUE(res && res->has_invite());
    const auto invite = res->invite().token();
    req.Clear();
    req.mutable_join_invite()->set_token(invite);
    res = manager.call(req);
    ASSERT_TRUE(res && res->has_server());
    req.Clear();
    auto* override = req.mutable_set_override()->mutable_override();
    override->set_channel_id(channel);
    override->set_target_type(proto::PermissionOverride::TARGET_USER);
    override->set_target_id(auth->user().id());
    override->set_allow(permissions::ManageChannel);
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_ok());
    req.Clear();
    req.mutable_create_webhook()->set_channel_id(channel);
    req.mutable_create_webhook()->set_name("delegated manager");
    res = manager.call(req);
    ASSERT_TRUE(res && res->has_incoming_webhook());
    const auto delegated = res->incoming_webhook();
    const auto token = QByteArray::fromStdString(delegated.token());
    EXPECT_TRUE(send(request(token, R"({"content":"authorized"})")).startsWith("HTTP/1.1 201"));
    req.Clear();
    req.mutable_create_channel()->set_server_id(community);
    req.mutable_create_channel()->set_name("separate-channel");
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_channel());
    const auto other = res->channel().id();
    req.Clear();
    req.mutable_revoke_webhook()->set_channel_id(other);
    req.mutable_revoke_webhook()->set_webhook_id(delegated.id());
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_ok());
    EXPECT_TRUE(send(request(token, R"({"content":"still scoped"})")).startsWith("HTTP/1.1 201"));
    req.Clear();
    override = req.mutable_set_override()->mutable_override();
    override->set_channel_id(channel);
    override->set_target_type(proto::PermissionOverride::TARGET_USER);
    override->set_target_id(auth->user().id());
    override->set_deny(permissions::ManageChannel);
    res = owner->call(req);
    ASSERT_TRUE(res && res->has_ok());
    EXPECT_TRUE(send(request(token, R"({"content":"permission removed"})")).startsWith("HTTP/1.1 403"));
}
TEST_F(Webhooks, CredentialsPersistAndSuspensionStopsDelivery)
{
    const auto token = QByteArray::fromStdString(hook.token());
    owner.reset();
    server.stop();
    const auto creator = server.userId(QStringLiteral("webhook-owner"));
    ASSERT_NE(creator, 0u);
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(server.databasePath(), &error));
        const auto records = store.webhooks(channel);
        ASSERT_EQ(records.size(), 1u);
        EXPECT_NE(records[0].digest, token);
        EXPECT_EQ(records[0].digest.size(), 32);
        ASSERT_TRUE(store.setSuspended(creator, true));
    }
    ASSERT_TRUE(server.start());
    EXPECT_TRUE(send(request(token, R"({"content":"suspended"})")).startsWith("HTTP/1.1 403"));
    server.stop();
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(server.databasePath(), &error));
        ASSERT_TRUE(store.setSuspended(creator, false));
    }
    ASSERT_TRUE(server.start());
    EXPECT_TRUE(send(request(token, R"({"content":"after restart"})")).startsWith("HTTP/1.1 201"));
}

TEST(WebhookConfiguration, DisabledByDefault)
{
    TestServer server;
    ASSERT_TRUE(server.start());
    EXPECT_EQ(server.server()->webhookPort(), 0);
    RawClient client(server);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.hello());
    ASSERT_TRUE(client.registerUser("webhook-disabled", "webhook-password"));
    proto::Envelope req;
    req.mutable_create_webhook()->set_channel_id(1);
    req.mutable_create_webhook()->set_name("disabled");
    auto res = client.call(req);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->error().code(), proto::ERROR_BAD_REQUEST);
}

} // namespace
