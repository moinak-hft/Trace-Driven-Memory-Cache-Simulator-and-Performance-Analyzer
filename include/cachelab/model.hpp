#pragma once
#include "features.hpp"
#include <iomanip>

namespace cachelab {
struct Model {
    u64 horizon = 0, block_size = 0;
    double threshold = 0.5, bias = 0.0;
    Features weights{};
    static Model load(const std::string& path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("cannot open model: " + path);
        std::string key, schema; unsigned version = 0; std::size_t count = 0;
        Model m;
        if (!(input >> key >> version) || key != "CACHELAB_MODEL" || version != 1)
            throw std::runtime_error("unsupported model format");
        if (!(input >> key >> schema) || key != "schema" || schema != FEATURE_SCHEMA)
            throw std::runtime_error("model feature schema mismatch");
        if (!(input >> key >> count) || key != "features" || count != FEATURE_COUNT)
            throw std::runtime_error("model feature count mismatch");
        if (!(input >> key >> m.block_size) || key != "block_size" || !m.block_size)
            throw std::runtime_error("invalid model block size");
        if (!(input >> key >> m.horizon) || key != "horizon" || !m.horizon || m.horizon > 10000000)
            throw std::runtime_error("invalid model reuse horizon");
        if (!(input >> key >> m.threshold) || key != "threshold" || !std::isfinite(m.threshold) || m.threshold < 0 || m.threshold > 1)
            throw std::runtime_error("invalid model threshold");
        if (!(input >> key >> m.bias) || key != "bias" || !std::isfinite(m.bias))
            throw std::runtime_error("invalid model bias");
        if (!(input >> key) || key != "weights") throw std::runtime_error("missing model weights");
        for (auto& w : m.weights) if (!(input >> w) || !std::isfinite(w)) throw std::runtime_error("invalid model weights");
        if (input >> key) throw std::runtime_error("unexpected trailing model data");
        return m;
    }
    double predict(const Features& f) const {
        double z = bias;
        for (std::size_t i = 0; i < FEATURE_COUNT; ++i) z += weights[i] * f[i];
        // Stable sigmoid; no dependencies or subprocesses on the access path.
        return z >= 0 ? 1.0/(1.0+std::exp(-z)) : std::exp(z)/(1.0+std::exp(z));
    }
};
} // namespace cachelab
