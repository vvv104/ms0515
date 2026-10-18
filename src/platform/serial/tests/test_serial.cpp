/*
 * test_serial.cpp — what every host can check of SerialPort.
 */
#include <doctest/doctest.h>

#include "SerialPort.hpp"

#include <string>

using ms0515::serial::SerialPort;

TEST_CASE("a port that is not there fails, saying why") {
    std::string error;
    auto port = SerialPort::open("no-such-port-ms0515", 4800, error);
    CHECK(port == nullptr);
    CHECK_FALSE(error.empty());
    CHECK(error.find("no-such-port-ms0515") != std::string::npos);
}

TEST_CASE("the host's ports are listed by the names open() takes") {
    const auto ports = ms0515::serial::listPorts();
    for (const auto &name : ports)
        CHECK_FALSE(name.empty());
}
