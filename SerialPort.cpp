#include "SerialPort.hpp"

#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <termios.h>
#include <unistd.h>

namespace
{
[[noreturn]] void throwErrno(const std::string& what)
{
    throw std::system_error(errno, std::generic_category(), what);
}
} // namespace

SerialPort::SerialPort(const std::string& path)
{
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0)
    {
        throwErrno("open " + path);
    }

    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0)
    {
        const int saved = errno;
        ::close(fd_);
        errno = saved;
        throwErrno("tcgetattr " + path);
    }
    ::cfmakeraw(&tty);
    tty.c_cc[VMIN] = 1;  // block until at least one byte
    tty.c_cc[VTIME] = 0; // no inter-byte timeout
    if (::tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        const int saved = errno;
        ::close(fd_);
        errno = saved;
        throwErrno("tcsetattr " + path);
    }
}

SerialPort::~SerialPort()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

void SerialPort::writeAll(const std::uint8_t* data, std::size_t size)
{
    while (size > 0)
    {
        const ssize_t written = ::write(fd_, data, size);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throwErrno("write");
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

std::size_t SerialPort::readSome(std::uint8_t* data, std::size_t capacity)
{
    while (true)
    {
        const ssize_t got = ::read(fd_, data, capacity);
        if (got >= 0)
        {
            return static_cast<std::size_t>(got);
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EIO)
        {
            return 0; // pty peer went away
        }
        throwErrno("read");
    }
}
