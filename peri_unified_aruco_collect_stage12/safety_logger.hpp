#pragma once
#include "encoder.hpp"
#include <fstream>
#include <string>
// Compatibility with peri_AXI_collect_safe's emergency_stop.csv schema.
// No camera capture is attempted on abnormal termination to avoid additional
// blocking during an emergency; image/ArUco fields remain empty/unavailable.
class SafetyLogger {
public:
    explicit SafetyLogger(const std::string& filename);
    void log_abort(const std::string& timestamp,const std::string& reason,
                   int event_number,int last_step_id,int posture,
                   const EncoderSample& enc);
private:
    std::ofstream out_;
};
