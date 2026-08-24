#ifndef EMERGENCY_MONITOR_HPP
#define EMERGENCY_MONITOR_HPP

#include <atomic>
#include <termios.h>
#include <thread>

class EmergencyKeyMonitor {
public:
    EmergencyKeyMonitor() = default;
    ~EmergencyKeyMonitor();

    EmergencyKeyMonitor(const EmergencyKeyMonitor&) = delete;
    EmergencyKeyMonitor& operator=(const EmergencyKeyMonitor&) = delete;

    // Enables an Enter-free emergency key while auto replay is running.
    // Returns false when stdin is not a terminal or its mode cannot be changed.
    bool start();
    void stop();

    bool requested() const;

private:
    void run();

    std::atomic<bool> running_{false};
    std::atomic<bool> requested_{false};
    std::thread worker_;
    termios original_termios_{};
    bool terminal_configured_ = false;
};

#endif
