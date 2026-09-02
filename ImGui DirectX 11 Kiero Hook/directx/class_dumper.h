#pragma once
#include "../explorer/explorer.h"

namespace ClassDumper {
    bool Dump(Class* klass);
    const char* GetStatus();
    bool LastSucceeded();
}
