#pragma once

#include <optional>
#include <string>

// Small single-client TCP line server for PC-directed feedback commands.
// Each received/sent protocol message is UTF-8/ASCII text terminated by '\n'.
class FeedbackTcpServer {
public:
    explicit FeedbackTcpServer(int port);
    ~FeedbackTcpServer();
    FeedbackTcpServer(const FeedbackTcpServer&) = delete;
    FeedbackTcpServer& operator=(const FeedbackTcpServer&) = delete;

    void listen();
    bool accept_client(int timeout_ms);
    std::optional<std::string> read_line(int timeout_ms);
    void send_line(const std::string& line);
    bool connected() const { return client_fd_ >= 0; }
    void close_client();

private:
    int port_;
    int listen_fd_ = -1;
    int client_fd_ = -1;
    std::string receive_buffer_;
};
