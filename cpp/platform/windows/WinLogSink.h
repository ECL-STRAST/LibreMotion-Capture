#pragma once
#include "platform/Interfaces.h"
namespace lmc { class WinLogSink : public ILogSink { public: void log(int level, const char* msg) override; }; }
