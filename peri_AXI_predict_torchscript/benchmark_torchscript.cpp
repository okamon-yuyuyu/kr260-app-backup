#include "torchscript_predictor.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string model_path;
    std::string config_path;
    std::string image_path;
    std::string output_csv;
    int runs = 100;
    int warmup_runs = 10;
    int torch_threads = 4;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after " + arg);
            }
            return argv[++i];
        };
        if (arg == "--model") {
            options.model_path = value();
        } else if (arg == "--config") {
            options.config_path = value();
        } else if (arg == "--image") {
            options.image_path = value();
        } else if (arg == "--output") {
            options.output_csv = value();
        } else if (arg == "--runs") {
            options.runs = std::stoi(value());
        } else if (arg == "--warmup") {
            options.warmup_runs = std::stoi(value());
        } else if (arg == "--torch-threads") {
            options.torch_threads = std::stoi(value());
        } else if (arg == "--help") {
            std::cout
                << "Usage: " << argv[0] << " --model FILE --config FILE --image FILE [options]\n"
                << "  --runs N            measured runs (default: 100)\n"
                << "  --warmup N          warm-up runs (default: 10)\n"
                << "  --torch-threads N   LibTorch CPU threads (default: 4)\n"
                << "  --output FILE       optional per-run CSV\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (options.model_path.empty() || options.config_path.empty() ||
        options.image_path.empty()) {
        throw std::invalid_argument("--model, --config and --image are required");
    }
    if (options.runs <= 0 || options.warmup_runs < 0 || options.torch_threads <= 0) {
        throw std::invalid_argument("invalid runs/warmup/torch-threads value");
    }
    return options;
}

double mean(const std::vector<double>& values) {
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double percentile(std::vector<double> values, double ratio) {
    std::sort(values.begin(), values.end());
    const double position = (values.size() - 1) * ratio;
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(lower + 1, values.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

void print_summary(const std::string& name, const std::vector<double>& values) {
    const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
    std::cout << name
              << ": mean=" << mean(values)
              << " ms median=" << percentile(values, 0.50)
              << " ms p95=" << percentile(values, 0.95)
              << " ms min=" << *minimum
              << " ms max=" << *maximum << " ms\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const cv::Mat frame = cv::imread(options.image_path, cv::IMREAD_COLOR);
        if (frame.empty()) {
            throw std::runtime_error("failed to read image: " + options.image_path);
        }

        TorchScriptPredictor predictor(
            options.model_path, options.config_path,
            options.torch_threads, options.warmup_runs);

        std::vector<PredictionResult> results;
        results.reserve(options.runs);
        for (int i = 0; i < options.runs; ++i) {
            results.push_back(predictor.predict(frame));
        }

        std::vector<double> preprocess_times;
        std::vector<double> inference_times;
        std::vector<double> total_times;
        preprocess_times.reserve(results.size());
        inference_times.reserve(results.size());
        total_times.reserve(results.size());
        for (const auto& result : results) {
            preprocess_times.push_back(result.preprocess_ms);
            inference_times.push_back(result.inference_ms);
            total_times.push_back(result.total_ms);
        }

        const ModelConfig& config = predictor.config();
        const PredictionResult& last = results.back();
        std::cout << std::fixed << std::setprecision(3)
                  << "backend: TorchScript / LibTorch CPU\n"
                  << "architecture: " << config.architecture << '\n'
                  << "model: " << config.model_type << '\n'
                  << "input: " << config.input_channels << 'x'
                  << config.image_height << 'x' << config.image_width << '\n'
                  << "runs: " << options.runs
                  << " warmup: " << options.warmup_runs
                  << " threads: " << options.torch_threads << '\n'
                  << "prediction: cmd" << last.command
                  << " confidence=" << last.confidence << '\n';
        print_summary("preprocess", preprocess_times);
        print_summary("inference", inference_times);
        print_summary("total", total_times);

        if (!options.output_csv.empty()) {
            std::ofstream output(options.output_csv);
            if (!output) {
                throw std::runtime_error("failed to open output CSV: " + options.output_csv);
            }
            output << "run,label,command,confidence,preprocess_ms,inference_ms,total_ms\n";
            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& result = results[i];
                output << i << ',' << result.label << ',' << result.command << ','
                       << result.confidence << ',' << result.preprocess_ms << ','
                       << result.inference_ms << ',' << result.total_ms << '\n';
            }
            std::cout << "CSV: " << options.output_csv << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
