#pragma once

#include <string>

namespace socketify {

struct SSLConfig {
    std::string cert_file;
    std::string key_file;
    bool enabled = false;
};

}
