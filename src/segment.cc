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


#include <algorithm>
#include <random>
#include <unistd.h>
#include "segment.h"
#include "manager.h"
#include "cross_process_bag.h"
#include "json_builder.h"

Segment::Segment(const std::string &serviceId, const std::string &serviceInstanceId, int version,
                 const std::string &header) {

    _serviceId = serviceId;
    _serviceInstanceId = serviceInstanceId;
    _header = header;
    _version = version;

    static std::random_device dev;
    static std::mt19937 rng(dev());

    std::string traceId = Manager::generateUUID() + "." + std::to_string(getpid()) + "." + std::to_string(rng());
    traceId.erase(std::remove(traceId.begin(), traceId.end(), '-'), traceId.end());

    Segment::_traceId = traceId;
    Segment::_traceSegmentId = traceId;
    bag = new CrossProcessBag(_serviceId, _serviceInstanceId, traceId, _version, _header);

    if (!bag->getTraceId().empty()) {
        Segment::_traceId = bag->getTraceId();
    }
}

Segment::~Segment() {
    delete bag;

    for (auto span : spans) {
        delete span;
    }
    spans.clear();
    spans.shrink_to_fit();
}

std::string Segment::marshal() {
    if (!spans.empty()) {
        auto span = spans.front();
        span->setEndTIme();
        if (_status_code >= 400) {
            span->setIsError(true);
        }
        span->pushTag(new Tag("status_code", std::to_string(_status_code)));
    }

    JsonBuilder json;

    // 基本字段
    json.addKeyValue("traceId", _traceId);
    json.addKeyValue("traceSegmentId", _traceSegmentId);
    json.addKeyValue("service", _serviceId);
    json.addKeyValue("serviceInstance", _serviceInstanceId);
    //json.addKeyValue("isSizeLimited", _isSizeLimited);

    // Spans 数组
    json.startArray("spans");
    for (auto span: spans) {
        json.startObject();

        // Span 基本字段
        json.addKeyValue("spanId", span->getSpanId());
        json.addKeyValue("parentSpanId", span->getParentSpanId());
        json.addKeyValue("startTime", span->getStartTime());
        json.addKeyValue("endTime", span->getEndTime());
        json.addKeyValue("operationName", span->getOperationName());

        // Peer
        if (!span->getPeer().empty()) {
            json.addKeyValue("peer", span->getPeer());
        }

        // Span Type
        json.addKeyValue("spanType", static_cast<int>(span->getSpanType()));
        json.addKeyValue("spanLayer", static_cast<int>(span->getSpanLayer()));
        json.addKeyValue("componentId", span->getComponentId());

        // Error flag
        json.addKeyValue("isError", span->getIsError());
        json.addKeyValue("skipAnalysis", span->getSkipAnalysis());

        // Tags
        if (!span->getTags().empty()) {
            json.startArray("tags");
            for (auto& tag: span->getTags()) {
                json.startObject();
                json.addKeyValue("key", tag->getKey());
                json.addKeyValue("value", tag->getValue());
                json.endObject();
            }
            json.endArray();
        }

        // Logs
        if (!span->getLogs().empty()) {
            json.startArray("logs");
            for (auto& log: span->getLogs()) {
                json.startObject();
                json.addKeyValue("time", log->getTime());
                json.startArray("data");
                json.startObject();
                json.addKeyValue("key", log->getKey());
                json.addKeyValue("value", log->getValue());
                json.endObject();
                json.endArray();
                json.endObject();
            }
            json.endArray();
        }

        // References
        if (!span->getRefs().empty()) {
            json.startArray("refs");
            for (auto& ref: span->getRefs()) {
                json.startObject();
                json.addKeyValue("refType", ref->getRefType());
                json.addKeyValue("traceId", ref->getTraceId());
                json.addKeyValue("parentTraceSegmentId", ref->getParentTraceSegmentId());
                json.addKeyValue("parentSpanId", ref->getParentSpanId());
                json.addKeyValue("parentService", ref->getParentService());
                json.addKeyValue("parentServiceInstance", ref->getParentServiceInstance());
                json.addKeyValue("parentEndpoint", ref->getParentEndpoint());
                json.addKeyValue("networkAddressUsedAtPeer", ref->getNetworkAddressUsedAtPeer());
                json.endObject();
            }
            json.endArray();
        }

        json.endObject();
    }
    json.endArray();

    return json.toString();
}

void Segment::setStatusCode(int code) {
    _status_code = code;
}

Span *Segment::createSpan(SkySpanType type, SkySpanLayer layer, int componentId) {
    Span *span = new Span();
    span->setSpanType(type);
    span->setSpanLayer(layer);
    span->setComponentId(componentId);

    int id = 0;
    int parentId = -1;
    if (!spans.empty()) {
        id = spans.back()->getSpanId() + 1;
        parentId = 0;
    }

    span->setSpanId(id);
    span->setParentSpanId(parentId);


    spans.push_back(span);
    return span;
}

Span *Segment::findOrCreateSpan(const std::string &name, SkySpanType type, SkySpanLayer layer, int componentId) {
    if (!spans.empty()) {
        for (auto item: spans) {
            if (item->getOperationName() == name) {
                return item;
            }
        }
    }
    auto span = createSpan(type, layer, componentId);
    span->setOperationName(name);
    return span;
}

Span *Segment::firstSpan() {
    return spans.at(0);
}

std::string Segment::createHeader(Span *span) {
    return bag->encode(span->getSpanId(), span->getPeer());
}

void Segment::createRefs() {
    if (!spans.empty()) {
        Span *sp = spans.front();
        bag->setOperationName(sp->getOperationName());

        if (!_header.empty() && !bag->getParentTraceSegmentId().empty()) {
            auto ref = new SkySegmentReference();
            ref->setRefType(0);
            ref->setTraceId(bag->getTraceId());
            ref->setParentTraceSegmentId(bag->getParentTraceSegmentId());
            ref->setParentSpanId(bag->getParentSpanId());
            ref->setParentService(bag->getParentService());
            ref->setParentServiceInstance(bag->getParentServiceInstance());
            ref->setParentEndpoint(bag->getParentEndpoint());
            ref->setNetworkAddressUsedAtPeer(bag->getNetworkAddressUsedAtPeer());
            sp->pushRefs(ref);
        }
    }
}

const std::string& Segment::getTraceId() {
    return _traceId;
}

void Segment::setSkip(bool skip) {
    this->doSkip = skip;
}

bool Segment::skip() {
    return doSkip;
}
