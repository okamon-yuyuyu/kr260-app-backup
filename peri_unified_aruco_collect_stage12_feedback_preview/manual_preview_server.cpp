#include "manual_preview_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
bool send_all(int fd, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool send_text(int fd, const std::string& status, const std::string& type,
               const std::string& body) noexcept {
    const std::string header =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: " + type + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n";
    return send_all(fd, header.data(), header.size()) &&
           send_all(fd, body.data(), body.size());
}

const char kPreviewPage[] = R"HTML(<!doctype html>
<html lang="ja"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>AXI manual camera preview</title>
<style>
body{margin:0;background:#17191d;color:#eee;font:16px system-ui,sans-serif}
header{padding:10px 16px;background:#25282e}
main{height:calc(100vh - 48px);display:flex;align-items:center;justify-content:center}
img{max-width:100%;max-height:100%;object-fit:contain}
#status{color:#b7c3d0;font-size:14px;margin-left:12px}
</style></head>
<body><header>AXI manual 撮影画像 <span id="status">画像待機中</span></header>
<main><img id="frame" alt="撮影画像がまだありません"></main>
<script>
const frame=document.getElementById('frame'),status=document.getElementById('status');
function refresh(){
  frame.onload=()=>status.textContent='最新画像を表示中';
  frame.onerror=()=>status.textContent='撮影画像を待っています';
  frame.src='/frame.jpg?t='+Date.now();
}
refresh();setInterval(refresh,500);
</script></body></html>)HTML";
}

ManualPreviewServer::ManualPreviewServer(std::uint16_t port) : port_(port) {}

ManualPreviewServer::~ManualPreviewServer() {
    stopping_.store(true);
    if (server_fd_ >= 0) ::shutdown(server_fd_, SHUT_RDWR);
    if (worker_.joinable()) worker_.join();
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
}

void ManualPreviewServer::start() {
    if (server_fd_ >= 0) return;
    server_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd_ < 0)
        throw std::runtime_error(std::string("Preview socket failed: ") + std::strerror(errno));
    int reuse = 1;
    (void)::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        const std::string reason = std::strerror(errno);
        ::close(server_fd_); server_fd_ = -1;
        throw std::runtime_error("Cannot bind preview server to 127.0.0.1:" +
                                 std::to_string(port_) + ": " + reason);
    }
    if (::listen(server_fd_, 4) < 0) {
        const std::string reason = std::strerror(errno);
        ::close(server_fd_); server_fd_ = -1;
        throw std::runtime_error("Cannot listen on preview port: " + reason);
    }
    stopping_.store(false);
    worker_ = std::thread(&ManualPreviewServer::serve, this);
}

void ManualPreviewServer::publish(const cv::Mat& frame) {
    if (frame.empty()) throw std::runtime_error("Cannot preview an empty frame");
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".jpg", frame, encoded, {cv::IMWRITE_JPEG_QUALITY, 85}))
        throw std::runtime_error("Could not encode the manual preview JPEG");
    std::lock_guard<std::mutex> lock(image_mutex_);
    latest_jpeg_.swap(encoded);
}

void ManualPreviewServer::serve() noexcept {
    while (!stopping_.load()) {
        fd_set read_set;
        FD_ZERO(&read_set); FD_SET(server_fd_, &read_set);
        timeval timeout{0, 250000};
        const int ready = ::select(server_fd_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (stopping_.load()) break;
        if (ready < 0) { if (errno == EINTR) continue; break; }
        if (ready == 0 || !FD_ISSET(server_fd_, &read_set)) continue;
        const int client = ::accept4(server_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) { if (errno == EINTR || stopping_.load()) continue; continue; }
        // Port forwarders and browser probes may open a TCP connection before
        // sending an HTTP request. Never let one idle client block later requests.
        timeval client_timeout{1, 0};
        (void)::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                           &client_timeout, sizeof(client_timeout));
        (void)::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                           &client_timeout, sizeof(client_timeout));
        handle_client(client);
        ::close(client);
    }
}

void ManualPreviewServer::handle_client(int client) noexcept {
    char buffer[2048];
    const ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
    if (n <= 0) return;
    const std::string request(buffer, static_cast<std::size_t>(n));
    const auto first_space = request.find(' ');
    const auto second_space = first_space == std::string::npos
        ? std::string::npos : request.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        (void)send_text(client, "400 Bad Request", "text/plain; charset=utf-8", "Bad request");
        return;
    }
    const std::string path = request.substr(first_space + 1, second_space - first_space - 1);
    if (path == "/") {
        (void)send_text(client, "200 OK", "text/html; charset=utf-8", kPreviewPage);
        return;
    }
    if (path.rfind("/frame.jpg", 0) == 0) {
        std::vector<unsigned char> jpeg;
        { std::lock_guard<std::mutex> lock(image_mutex_); jpeg = latest_jpeg_; }
        if (jpeg.empty()) {
            (void)send_text(client, "503 Service Unavailable",
                            "text/plain; charset=utf-8", "Waiting for first camera frame");
            return;
        }
        const std::string header =
            "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: " +
            std::to_string(jpeg.size()) +
            "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        (void)send_all(client, header.data(), header.size());
        (void)send_all(client, jpeg.data(), jpeg.size());
        return;
    }
    (void)send_text(client, "404 Not Found", "text/plain; charset=utf-8", "Not found");
}
