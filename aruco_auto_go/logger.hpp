#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "command.hpp"
#include "encoder.hpp"

#include <fstream>
#include <string>

class Logger {
public:
    explicit Logger(const std::string& csv_path);
    void log(int index,
             const std::string& timestamp,
             const std::string& image_path,
             const PeristalsisCommand& cmd,
             const EncoderValues& raw,
             const EncoderValues& diff);

private:
    std::ofstream ofs_;
};

std::string now_string();
std::string make_image_filename(const std::string& dir, int index, int id);
#endif
