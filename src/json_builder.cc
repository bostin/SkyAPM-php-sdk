/*
 * Copyright 2021 SkyAPM
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "json_builder.h"
#include <iomanip>
#include <sstream>

JsonBuilder::JsonBuilder() {
    buffer << "{";
    firstElement = true;
}

void JsonBuilder::startObject() {
    addCommaIfNeeded();
    buffer << "{";
    firstElement = true;
}

void JsonBuilder::endObject() {
    buffer << "}";
    firstElement = false;
}

void JsonBuilder::startArray(const std::string& key) {
    addCommaIfNeeded();
    buffer << "\"" << key << "\":[";
    firstElement = true;
}

void JsonBuilder::endArray() {
    buffer << "]";
    firstElement = false;
}

void JsonBuilder::addKeyValue(const std::string& key, int value) {
    addCommaIfNeeded();
    buffer << "\"" << key << "\":" << value;
}

void JsonBuilder::addKeyValue(const std::string& key, long value) {
    addCommaIfNeeded();
    buffer << "\"" << key << "\":" << value;
}

void JsonBuilder::addKeyValue(const std::string& key, bool value) {
    addCommaIfNeeded();
    buffer << "\"" << key << "\":" << (value ? "true" : "false");
}

void JsonBuilder::addKeyValue(const std::string& key, const std::string& value) {
    addCommaIfNeeded();
    buffer << "\"" << key << "\":";
    std::string escaped;
    escapeJsonString(value, escaped);
    buffer << "\"" << escaped << "\"";
}

void JsonBuilder::addKeyNull(const std::string& key) {
    addCommaIfNeeded();
    buffer << "\"" << key << "\":null";
}

std::string JsonBuilder::toString() {
    buffer << "}";
    return buffer.str();
}

void JsonBuilder::addCommaIfNeeded() {
    if (!firstElement) {
        buffer << ",";
    }
    firstElement = false;
}

void JsonBuilder::escapeJsonString(const std::string& str, std::string& out) {
    for (char c : str) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
}
