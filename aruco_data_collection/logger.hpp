#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "aruco_detector.hpp"
#include "command.hpp"
#include "encoder.hpp"

#include <fstream>
#include <string>

class Logger {
public:
    explicit Logger(const std::string& csv_path);

    void log_action(int index,
                    const std::string& timestamp,
                    const std::string& mode,
                    const std::string& image_path,
                    const PeristalsisCommand& cmd,
                    const EncoderValues& raw,
                    const EncoderValues& diff,
                    const ArucoResult& aruco);

    void log_goal(int index,
                  const std::string& timestamp,
                  const std::string& mode,
                  const std::string& image_path,
                  const EncoderValues& raw,
                  const EncoderValues& diff,
                  const ArucoResult& aruco);

private:
    std::ofstream ofs_;

    void write_common(int index,
                      const std::string& timestamp,
                      const std::string& mode,
                      const std::string& event,
                      const std::string& image_path,
                      const PeristalsisCommand& cmd,
                      const EncoderValues& raw,
                      const EncoderValues& diff,
                      const ArucoResult& aruco);
};

std::string now_string();
std::string make_image_filename(const std::string& dir, int index, int id);

#endif
