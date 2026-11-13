#include "feedback_tcp.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace {
bool wait_readable(int fd, int timeout_ms) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, timeout_ms);
    if (rc == 0) return false;
    if (rc < 0) {
        if (errno == EINTR) return false;
        throw std::runtime_error(std::string("TCP poll failed: ") + std::strerror(errno));
    }
    // Let recv()/accept() report EOF or the socket error for HUP/ERR too.
    return (pfd.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0;
}
}

FeedbackTcpServer::FeedbackTcpServer(int port) : port_(port) {
    if (port < 1 || port > 65535) throw std::runtime_error("Feedback TCP port out of range");
}

FeedbackTcpServer::~FeedbackTcpServer() {
    close_client();
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

void FeedbackTcpServer::listen() {
    if (listen_fd_ >= 0) return;
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        throw std::runtime_error(std::string("TCP socket failed: ") + std::strerror(errno));
    int yes = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        throw std::runtime_error(std::string("TCP setsockopt failed: ") + std::strerror(errno));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port_));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        throw std::runtime_error(std::string("TCP bind failed: ") + std::strerror(errno));
    if (::listen(listen_fd_, 1) < 0)
        throw std::runtime_error(std::string("TCP listen failed: ") + std::strerror(errno));
}

bool FeedbackTcpServer::accept_client(int timeout_ms) {
    if (listen_fd_ < 0) throw std::runtime_error("TCP server has not been started");
    if (client_fd_ >= 0) return true;
    if (!wait_readable(listen_fd_, timeout_ms)) return false;
    client_fd_ = ::accept(listen_fd_, nullptr, nullptr);
    if (client_fd_ < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return false;
        throw std::runtime_error(std::string("TCP accept failed: ") + std::strerror(errno));
    }
    receive_buffer_.clear();
    return true;
}

std::optional<std::string> FeedbackTcpServer::read_line(int timeout_ms) {
    if (client_fd_ < 0) throw std::runtime_error("No PC TCP client is connected");
    while (true) {
        const auto newline = receive_buffer_.find('\n');
        if (newline != std::string::npos) {
            std::string line = receive_buffer_.substr(0, newline);
            receive_buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        if (receive_buffer_.size() > 512) {
            close_client();
            throw std::runtime_error("Feedback TCP line exceeds 512 bytes");
        }
        if (!wait_readable(client_fd_, timeout_ms)) return std::nullopt;
        char chunk[256];
        const ssize_t n = ::recv(client_fd_, chunk, sizeof(chunk), 0);
        if (n == 0) {
            close_client();
            return std::nullopt;
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return std::nullopt;
            const std::string message = std::string("TCP recv failed: ") + std::strerror(errno);
            close_client();
            throw std::runtime_error(message);
        }
        receive_buffer_.append(chunk, static_cast<std::size_t>(n));
    }
}

void FeedbackTcpServer::send_line(const std::string& line) {
    if (client_fd_ < 0) throw std::runtime_error("No PC TCP client is connected");
    const std::string message = line + "\n";
    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t n = ::send(client_fd_, message.data() + sent, message.size() - sent,
                                 MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            const std::string error = std::string("TCP send failed: ") + std::strerror(errno);
            close_client();
            throw std::runtime_error(error);
        }
        if (n == 0) {
            close_client();
            throw std::runtime_error("TCP peer closed while sending");
        }
        sent += static_cast<std::size_t>(n);
    }
}

void FeedbackTcpServer::close_client() {
    if (client_fd_ >= 0) ::close(client_fd_);
    client_fd_ = -1;
    receive_buffer_.clear();
}
