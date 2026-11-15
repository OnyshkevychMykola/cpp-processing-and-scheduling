#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// A serial device (here: one end of a socat pty pair) in raw mode.
class SerialPort
{
public:
    // Throws std::system_error if the port cannot be opened or configured.
    explicit SerialPort(const std::string& path);
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Writes every byte, retrying on partial writes. Throws std::system_error.
    void writeAll(const std::uint8_t* data, std::size_t size);

    // Blocks until at least one byte is available; returns how many were read.
    // Returns 0 if the other side closed. Throws std::system_error.
    std::size_t readSome(std::uint8_t* data, std::size_t capacity);

private:
    int fd_ = -1;
};
