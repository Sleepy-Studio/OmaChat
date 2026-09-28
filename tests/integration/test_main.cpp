#include "omachat/core/Log.hpp"
#include "omachat/media/MediaPacket.hpp"

#include <QCoreApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    omachat::log::initialize("integration-tests", omachat::log::Level::Warning);
    omachat::media::initializeCrypto();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
