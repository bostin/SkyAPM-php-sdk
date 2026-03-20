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

#ifndef SKYWALKING_JSON_BUILDER_H
#define SKYWALKING_JSON_BUILDER_H

#include <string>
#include <vector>
#include <sstream>

class JsonBuilder {
public:
    JsonBuilder();
    void startObject();
    void endObject();
    void startArray(const std::string& key);
    void endArray();
    void addKeyValue(const std::string& key, int value);
    void addKeyValue(const std::string& key, long value);
    void addKeyValue(const std::string& key, bool value);
    void addKeyValue(const std::string& key, const std::string& value);
    void addKeyNull(const std::string& key);
    std::string toString();

private:
    std::ostringstream buffer;
    bool firstElement = true;
    void addCommaIfNeeded();
    void escapeJsonString(const std::string& str, std::string& out);
};

#endif
