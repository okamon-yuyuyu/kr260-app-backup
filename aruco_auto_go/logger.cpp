#include "logger.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

Logger::Logger(const std::string& csv_path) : ofs_(csv_path) {
    if (!ofs_) {
        throw std::runtime_error("failed to open log file: " + csv_path);
    }

    ofs_ << "index,timestamp,id,command_name,image_path,"
         << "rom0,rom1,rom2,rom3,"
         << "stroke_right,stroke_left,stroke_up,stroke_down,"
         << "stick1,stick3,"
         << "raw0,raw1,raw2,raw3,"
         << "diff0,diff1,diff2,diff3\n";
}

void Logger::log(int index,
                 const std::string& timestamp,
                 const std::string& image_path,
                 const PeristalsisCommand& cmd,
                 const EncoderValues& raw,
                 const EncoderValues& diff) {
    ofs_ << index << ','
         << timestamp << ','
         << cmd.id << ','
         << cmd.name << ','
         << image_path << ','
         << cmd.rom0 << ',' << cmd.rom1 << ',' << cmd.rom2 << ',' << cmd.rom3 << ','
         << cmd.stroke_right << ',' << cmd.stroke_left << ',' << cmd.stroke_up << ',' << cmd.stroke_down << ','
         << static_cast<int>(cmd.stick1) << ',' << static_cast<int>(cmd.stick3) << ','
         << raw.enc0 << ',' << raw.enc1 << ',' << raw.enc2 << ',' << raw.enc3 << ','
         << diff.enc0 << ',' << diff.enc1 << ',' << diff.enc2 << ',' << diff.enc3
         << '\n';

    ofs_.flush();
}

std::string now_string() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << '_' << std::setw(3) << std::setfill('0') << ms.count();

    return oss.str();
}

std::string make_image_filename(const std::string& dir, int index, int id) {
    std::ostringstream oss;

    oss << dir << "/img_"
        << std::setw(5) << std::setfill('0') << index
        << "_cmd" << id
        << "_" << now_string()
        << ".jpg";

    return oss.str();
}