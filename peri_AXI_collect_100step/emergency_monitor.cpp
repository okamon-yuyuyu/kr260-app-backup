#include "emergency_monitor.hpp"

#include <cerrno>
#include <poll.h>
#include <unistd.h>

EmergencyKeyMonitor::~EmergencyKeyMonitor() {
    stop();
}

bool EmergencyKeyMonitor::start() {
    if (running_) {
        return true;
    }

    requested_ = false;

    if (!isatty(STDIN_FILENO)) {
        return false;
    }

    if (tcgetattr(STDIN_FILENO, &original_termios_) != 0) {
        return false;
    }

    termios emergency_termios = original_termios_;
    emergency_termios.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    emergency_termios.c_cc[VMIN] = 0;
    emergency_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &emergency_termios) != 0) {
        return false;
    }

    terminal_configured_ = true;
    running_ = true;

    try {
        worker_ = std::thread(&EmergencyKeyMonitor::run, this);
    } catch (...) {
        running_ = false;
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios_);
        terminal_configured_ = false;
        throw;
    }

    return true;
}

void EmergencyKeyMonitor::stop() {
    running_ = false;

    if (worker_.joinable()) {
        worker_.join();
    }

    if (terminal_configured_) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios_);
        terminal_configured_ = false;
    }
}

bool EmergencyKeyMonitor::requested() const {
    return requested_.load();
}

void EmergencyKeyMonitor::run() {
    pollfd input{};
    input.fd = STDIN_FILENO;
    input.events = POLLIN;

    while (running_) {
        const int poll_result = poll(&input, 1, 20);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (poll_result == 0 || (input.revents & POLLIN) == 0) {
            continue;
        }

        char buffer[32];
        const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            continue;
        }

        for (ssize_t i = 0; i < count; ++i) {
            if (buffer[i] == 'e' || buffer[i] == 'E') {
                requested_ = true;
                running_ = false;
                break;
            }
        }
    }
}
