#include "crypto/E2E.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace omachat;

namespace {

proto::E2EBody body(const std::string& text)
{
    proto::E2EBody b;
    b.set_content(text);
    auto* f = b.add_files();
    f->set_attachment_id(7);
    f->set_filename("plans.pdf");
    f->set_key(std::string(32, 'k'));
    return b;
}

} // namespace

TEST(E2E, EveryAddressedDeviceOpensAndNobodyElse)
{
    const auto alice = e2e::Identity::generate(), bob = e2e::Identity::generate(), bobPhone = e2e::Identity::generate(),
               eve = e2e::Identity::generate();
    const e2e::Context ctx{100, 1};
    const auto sealed = e2e::seal(body("meet at 6"), ctx, alice,
        {bob.publicBytes(), bobPhone.publicBytes(), alice.publicBytes(), bob.publicBytes()});
    ASSERT_TRUE(sealed);
    for (const auto* who : {&alice, &bob, &bobPhone}) {
        const auto opened = e2e::open(*sealed, ctx, *who);
        ASSERT_TRUE(opened);
        EXPECT_EQ(opened->body.content(), "meet at 6");
        EXPECT_EQ(opened->body.files(0).filename(), "plans.pdf");
        EXPECT_EQ(opened->senderKey, alice.publicBytes());
    }
    EXPECT_FALSE(e2e::open(*sealed, ctx, eve)) << "not addressed to eve";
    proto::E2EPayload p;
    ASSERT_TRUE(p.ParseFromString(*sealed));
    EXPECT_EQ(p.wraps_size(), 3) << "duplicate recipients are wrapped once";
    EXPECT_EQ(p.ciphertext().find("meet at 6"), std::string::npos);
}

TEST(E2E, ContextAndTamperingAreDetected)
{
    const auto alice = e2e::Identity::generate(), bob = e2e::Identity::generate();
    const e2e::Context ctx{100, 1};
    const auto sealed = e2e::seal(body("hello"), ctx, alice, {bob.publicBytes()});
    ASSERT_TRUE(sealed);
    EXPECT_FALSE(e2e::open(*sealed, {101, 1}, bob)) << "moved to another conversation";
    EXPECT_FALSE(e2e::open(*sealed, {100, 2}, bob)) << "attributed to another author";

    proto::E2EPayload p;
    ASSERT_TRUE(p.ParseFromString(*sealed));
    auto flipped = p;
    flipped.mutable_ciphertext()->at(3) ^= 1;
    EXPECT_FALSE(e2e::open(flipped.SerializeAsString(), ctx, bob));

    // Claiming another sender key breaks the box.
    auto forged = p;
    forged.set_sender_key(e2e::Identity::generate().publicBytes().toStdString());
    EXPECT_FALSE(e2e::open(forged.SerializeAsString(), ctx, bob));

    EXPECT_FALSE(e2e::open("not a payload", ctx, bob));
    EXPECT_FALSE(e2e::seal(body("x"), ctx, alice, {}));
}

TEST(E2E, AParticipantCannotSwapTheBodyUnderSomeoneElsesWraps)
{
    // Carol can read Alice's message, so she learns its content key. She must
    // still not be able to make Bob believe Alice wrote something else.
    const auto alice = e2e::Identity::generate(), bob = e2e::Identity::generate(), carol = e2e::Identity::generate();
    const e2e::Context ctx{5, 1};
    const auto sealed = e2e::seal(body("original"), ctx, alice, {bob.publicBytes(), carol.publicBytes()});
    ASSERT_TRUE(sealed);
    const auto fake = e2e::seal(body("forged"), ctx, carol, {bob.publicBytes()});
    ASSERT_TRUE(fake);
    proto::E2EPayload real, other;
    ASSERT_TRUE(real.ParseFromString(*sealed));
    ASSERT_TRUE(other.ParseFromString(*fake));
    real.set_nonce(other.nonce());
    real.set_ciphertext(other.ciphertext());
    EXPECT_FALSE(e2e::open(real.SerializeAsString(), ctx, bob));
}

TEST(E2E, IdentitySurvivesStorage)
{
    const auto id = e2e::Identity::generate();
    const auto again = e2e::Identity::fromSecret(id.secretBytes());
    ASSERT_TRUE(again);
    EXPECT_EQ(again->publicBytes(), id.publicBytes());
    EXPECT_FALSE(e2e::Identity::fromSecret(QByteArray(5, 'x')));
}

TEST(E2E, FilesRoundTripAndRejectTamperingOrTruncation)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString plain = dir.filePath(QStringLiteral("plain.bin")), sealed = dir.filePath(QStringLiteral("f.enc")),
                  back = dir.filePath(QStringLiteral("back.bin"));
    QByteArray data;
    for (int i = 0; i < 200'000; ++i)
        data.append(static_cast<char>(i * 13));
    {
        QFile f(plain);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    QByteArray key;
    QString error;
    ASSERT_TRUE(e2e::encryptFile(plain, sealed, &key, &error)) << error.toStdString();
    EXPECT_EQ(static_cast<std::uint64_t>(QFileInfo(sealed).size()), e2e::encryptedSize(data.size()));
    ASSERT_TRUE(e2e::decryptFile(sealed, back, key, &error)) << error.toStdString();
    QFile b(back);
    ASSERT_TRUE(b.open(QIODevice::ReadOnly));
    EXPECT_EQ(b.readAll(), data);
    b.close();

    QByteArray wrongKey = key;
    wrongKey[0] = static_cast<char>(wrongKey[0] ^ 1);
    EXPECT_FALSE(e2e::decryptFile(sealed, back, wrongKey, &error));

    QFile s(sealed);
    ASSERT_TRUE(s.open(QIODevice::ReadWrite));
    QByteArray bytes = s.readAll();
    s.resize(bytes.size() - 100); // lose the final chunk's end
    s.close();
    EXPECT_FALSE(e2e::decryptFile(sealed, back, key, &error)) << "truncation is detected";
}

TEST(E2E, SafetyNumbersMatchOnBothSidesAndFollowKeys)
{
    const QByteArray a1(32, 'a'), a2(32, 'b'), b1(32, 'c');
    const QString n = e2e::safetyNumber(1, {a1, a2}, 2, {b1});
    EXPECT_EQ(n, e2e::safetyNumber(2, {b1}, 1, {a2, a1})) << "same from both ends, key order irrelevant";
    EXPECT_EQ(n, QStringLiteral("60386 21733 18440 13865 34198 52190 70934 68099 73903 08503 78776 68350")); // Independent BLAKE2b/BE64 vector.
    EXPECT_EQ(n.split(u' ').size(), 12);
    EXPECT_NE(n, e2e::safetyNumber(1, {a1}, 2, {b1})) << "a removed device changes it";
    EXPECT_NE(n, e2e::safetyNumber(1, {a1, a2}, 2, {QByteArray(32, 'd')}));
}

TEST(E2E, PortableFileBoundariesAndFailurePreserveExistingDestination)
{
    QTemporaryDir dir;
    const auto source = dir.filePath("source"), cipher = dir.filePath("cipher"), output = dir.filePath("output");
    for (const int size : {0, 1, 65535, 65536, 65537, 131072}) {
        QByteArray data(size, 'x');
        {
            QFile f(source);
            ASSERT_TRUE(f.open(QIODevice::WriteOnly));
            ASSERT_EQ(f.write(data), size);
        }
        QByteArray key;
        QString error;
        ASSERT_TRUE(e2e::encryptFile(source, cipher, &key, &error));
        EXPECT_EQ(static_cast<std::uint64_t>(QFileInfo(cipher).size()), e2e::encryptedSize(size));
        ASSERT_TRUE(e2e::decryptFile(cipher, output, key, &error));
        {
            QFile f(output);
            ASSERT_TRUE(f.open(QIODevice::ReadOnly));
            EXPECT_EQ(f.readAll(), data);
        }
        {
            QFile f(cipher);
            ASSERT_TRUE(f.open(QIODevice::Append));
            f.write("trailing");
        }
        EXPECT_FALSE(e2e::decryptFile(cipher, output, key, &error));
        {
            QFile f(output);
            ASSERT_TRUE(f.open(QIODevice::ReadOnly));
            EXPECT_EQ(f.readAll(), data);
        }
    }
}
