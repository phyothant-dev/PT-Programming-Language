#pragma once
#include "interpreter.h"
#include <string>

PTValue jsonParse(const std::string& json);
std::string jsonSerialize(const PTValue& val);
