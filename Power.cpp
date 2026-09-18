/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "android.hardware.power-service-qti"

#include "Power.h"
#include "PowerHintSession.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <climits>
#include <cstdlib>
#include <fmq/AidlMessageQueue.h>
#include <fmq/EventFlag.h>
#include <thread>

#include <aidl/android/hardware/power/BnPower.h>

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::aidl::android::hardware::power::BnPower;
using ::aidl::android::hardware::power::Boost;
using ::aidl::android::hardware::power::ChannelMessage;
using ::aidl::android::hardware::power::IPower;
using ::aidl::android::hardware::power::Mode;
using ::android::AidlMessageQueue;

using ::ndk::ScopedAStatus;
using ::ndk::SharedRefBase;

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace impl {

#ifdef MODE_EXT
extern bool isDeviceSpecificModeSupported(Mode type, bool* _aidl_return);
extern bool setDeviceSpecificMode(Mode type, bool enabled);
#endif

static std::string resolveCanonical(const std::string& path) {
    char resolved[PATH_MAX];
    return realpath(path.c_str(), resolved) ? resolved : path;
}

static Power* sInstance = nullptr;

Power::Power() : BnPower() {
    sInstance = this;
    power_init();
}

Power::~Power() {
    if (sInstance == this) {
        sInstance = nullptr;
    }
}

extern "C" void power_enforce_node_ceilings(void) {
    if (sInstance) {
        sInstance->applyCeilings();
    }
}

void setInteractive(bool interactive) {
    set_interactive(interactive ? 1 : 0);
}

ndk::ScopedAStatus Power::setMode(Mode type, bool enabled) {
    LOG(INFO) << "Power setMode: " << static_cast<int32_t>(type) << " to: " << enabled;
#ifdef MODE_EXT
    if (setDeviceSpecificMode(type, enabled)) {
        return ndk::ScopedAStatus::ok();
    }
#endif
    switch (type) {
#ifdef TAP_TO_WAKE_NODE
        case Mode::DOUBLE_TAP_TO_WAKE:
            ::android::base::WriteStringToFile(enabled ? "1" : "0", TAP_TO_WAKE_NODE, true);
            break;
#else
        case Mode::DOUBLE_TAP_TO_WAKE:
#endif
        case Mode::LOW_POWER:
        case Mode::DEVICE_IDLE:
        case Mode::DISPLAY_INACTIVE:
        case Mode::AUDIO_STREAMING_LOW_LATENCY:
        case Mode::CAMERA_STREAMING_SECURE:
        case Mode::CAMERA_STREAMING_LOW:
        case Mode::CAMERA_STREAMING_MID:
        case Mode::CAMERA_STREAMING_HIGH:
        case Mode::VR:
            LOG(INFO) << "Mode " << static_cast<int32_t>(type) << "Not Supported";
            break;
        case Mode::EXPENSIVE_RENDERING:
            set_expensive_rendering(enabled);
            break;
        case Mode::LAUNCH:
            power_hint(POWER_HINT_LAUNCH, enabled ? &enabled : NULL);
            break;
        case Mode::INTERACTIVE:
            setInteractive(enabled);
            break;
        case Mode::SUSTAINED_PERFORMANCE:
        case Mode::FIXED_PERFORMANCE:
            power_hint(POWER_HINT_SUSTAINED_PERFORMANCE, NULL);
            break;
        default:
            LOG(INFO) << "Mode " << static_cast<int32_t>(type) << "Not Supported";
            break;
    }
    applyCeilings();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isModeSupported(Mode type, bool* _aidl_return) {
    LOG(INFO) << "Power isModeSupported: " << static_cast<int32_t>(type);
#ifdef MODE_EXT
    if (isDeviceSpecificModeSupported(type, _aidl_return)) {
        return ndk::ScopedAStatus::ok();
    }
#endif
    switch (type) {
        case Mode::EXPENSIVE_RENDERING:
            if (is_expensive_rendering_supported()) {
                *_aidl_return = true;
            } else {
                *_aidl_return = false;
            }
            break;
#ifdef TAP_TO_WAKE_NODE
        case Mode::DOUBLE_TAP_TO_WAKE:
#endif
        case Mode::LAUNCH:
        case Mode::INTERACTIVE:
        case Mode::SUSTAINED_PERFORMANCE:
        case Mode::FIXED_PERFORMANCE:
            *_aidl_return = true;
            break;
        default:
            *_aidl_return = false;
            break;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    LOG(INFO) << "Power setBoost: " << static_cast<int32_t>(type) << ", duration: " << durationMs;
    switch (type) {
        case Boost::INTERACTION:
            power_hint(POWER_HINT_INTERACTION, &durationMs);
            break;
        default:
            LOG(INFO) << "Boost " << static_cast<int32_t>(type) << "Not Supported";
            break;
    }
    applyCeilings();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isBoostSupported(Boost type, bool* _aidl_return) {
    LOG(INFO) << "Power isBoostSupported: " << static_cast<int32_t>(type);
    switch (type) {
        case Boost::INTERACTION:
            *_aidl_return = true;
            break;
        default:
            *_aidl_return = false;
            break;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::createHintSession(int32_t tgid, int32_t uid,
                                            const std::vector<int32_t>& threadIds,
                                            int64_t durationNanos,
                                            std::shared_ptr<IPowerHintSession>* _aidl_return) {
    LOG(INFO) << "Power createHintSession";
    if (threadIds.size() == 0) {
        LOG(ERROR) << "Error: threadIds.size() shouldn't be " << threadIds.size();
        *_aidl_return = nullptr;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    *_aidl_return = setPowerHintSession(tgid, uid, threadIds, durationNanos);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::createHintSessionWithConfig(
        int32_t tgid, int32_t uid, const std::vector<int32_t>& threadIds, int64_t durationNanos,
        SessionTag, SessionConfig* config, std::shared_ptr<IPowerHintSession>* _aidl_return) {
    auto out = createHintSession(tgid, uid, threadIds, durationNanos, _aidl_return);
    static_cast<PowerHintSessionImpl*>(_aidl_return->get())->getSessionConfig(config);
    return out;
}

ndk::ScopedAStatus Power::getSessionChannel(int32_t, int32_t, ChannelConfig* _aidl_return) {
    static AidlMessageQueue<ChannelMessage, SynchronizedReadWrite> stubQueue{20, true};
    static std::thread stubThread([&] {
        ChannelMessage data;
        // This loop will only run while there is data waiting
        // to be processed, and blocks on a futex all other times
        while (stubQueue.readBlocking(&data, 1, 0)) {
        }
    });
    _aidl_return->channelDescriptor = stubQueue.dupeDesc();
    _aidl_return->readFlagBitmask = 0x01;
    _aidl_return->writeFlagBitmask = 0x02;
    _aidl_return->eventFlagDescriptor = std::nullopt;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::closeSessionChannel(int32_t, int32_t) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getHintSessionPreferredRate(int64_t* outNanoseconds) {
    LOG(INFO) << "Power getHintSessionPreferredRate";
    *outNanoseconds = getSessionPreferredRate();
    return ndk::ScopedAStatus::ok();
}

template <class T>
constexpr size_t enum_size() {
    return static_cast<size_t>(*(ndk::enum_range<T>().end() - 1)) + 1;
}

template <class E>
int64_t bitsForEnum() {
    return static_cast<int64_t>(std::bitset<enum_size<E>()>().set().to_ullong());
}

ndk::ScopedAStatus Power::getSupportInfo(SupportInfo* _aidl_return) {
    LOG(INFO) << "Power getSupportInfo";
    static SupportInfo supportInfo = {.usesSessions = false,
                                      .boosts = bitsForEnum<Boost>(),
                                      .modes = bitsForEnum<Mode>(),
                                      .sessionHints = 0,
                                      .sessionModes = 0,
                                      .sessionTags = 0,
                                      .compositionData =
                                              {
                                                      .isSupported = false,
                                                      .disableGpuFences = false,
                                                      .maxBatchSize = 1,
                                                      .alwaysBatch = false,
                                              },
                                      .headroom = {
                                              .isCpuSupported = false,
                                              .isGpuSupported = false,
                                              .cpuMinIntervalMillis = 0,
                                              .gpuMinIntervalMillis = 0,
                                      }};
    *_aidl_return = supportInfo;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getCpuHeadroom(const CpuHeadroomParams&, CpuHeadroomResult*) {
    LOG(INFO) << "Power getCpuHeadroom";
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getGpuHeadroom(const GpuHeadroomParams&, GpuHeadroomResult*) {
    LOG(INFO) << "Power getGpuHeadroom";
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::sendCompositionData(const std::vector<CompositionData>&) {
    LOG(INFO) << "Power sendCompositionData";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::sendCompositionUpdate(const CompositionUpdate&) {
    LOG(INFO) << "Power sendCompositionUpdate";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::setNodeCeiling(const std::string& in_nodePath, int64_t in_maxCeiling,
                                        int64_t in_minFloor) {
    LOG(INFO) << "Power setNodeCeiling: " << in_nodePath << " max: " << in_maxCeiling
              << " min: " << in_minFloor;

    if (in_nodePath.empty()) {
        return ndk::ScopedAStatus::ok();
    }

    if (in_maxCeiling <= 0 && in_minFloor <= 0) {
        return clearNodeCeiling(in_nodePath);
    }

    std::string canonicalPath = resolveCanonical(in_nodePath);
    std::lock_guard<std::mutex> lock(mCeilingLock);

    auto it = mNodeCeilings.find(canonicalPath);
    if (it == mNodeCeilings.end()) {
        std::string currentVal;
        if (::android::base::ReadFileToString(canonicalPath, &currentVal, true)) {
            currentVal = ::android::base::Trim(currentVal);
        }
        NodeCeilingInfo info;
        info.canonicalPath = canonicalPath;
        info.defaultValue = currentVal;
        info.maxCeiling = in_maxCeiling;
        info.minFloor = in_minFloor;
        mNodeCeilings[canonicalPath] = info;
    } else {
        it->second.maxCeiling = in_maxCeiling;
        it->second.minFloor = in_minFloor;
    }

    std::string currentVal;
    if (::android::base::ReadFileToString(canonicalPath, &currentVal, true)) {
        currentVal = ::android::base::Trim(currentVal);
        char* end = nullptr;
        int64_t val = strtoll(currentVal.c_str(), &end, 10);
        if (end != currentVal.c_str() && *end == '\0') {
            int64_t clamped = val;
            if (in_maxCeiling > 0 && clamped > in_maxCeiling) clamped = in_maxCeiling;
            if (in_minFloor > 0 && clamped < in_minFloor) clamped = in_minFloor;
            ::android::base::WriteStringToFile(std::to_string(clamped), canonicalPath, true);
        } else {
            int64_t target = in_maxCeiling > 0 ? in_maxCeiling : in_minFloor;
            ::android::base::WriteStringToFile(std::to_string(target), canonicalPath, true);
        }
    } else {
        int64_t target = in_maxCeiling > 0 ? in_maxCeiling : in_minFloor;
        ::android::base::WriteStringToFile(std::to_string(target), canonicalPath, true);
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::clearNodeCeiling(const std::string& in_nodePath) {
    LOG(INFO) << "Power clearNodeCeiling: " << in_nodePath;

    if (in_nodePath.empty()) {
        return ndk::ScopedAStatus::ok();
    }

    std::string canonicalPath = resolveCanonical(in_nodePath);
    std::lock_guard<std::mutex> lock(mCeilingLock);

    auto it = mNodeCeilings.find(canonicalPath);
    if (it != mNodeCeilings.end()) {
        if (!it->second.defaultValue.empty()) {
            ::android::base::WriteStringToFile(it->second.defaultValue, canonicalPath, true);
        }
        mNodeCeilings.erase(it);
    }

    return ndk::ScopedAStatus::ok();
}

void Power::applyCeilings() {
    std::lock_guard<std::mutex> lock(mCeilingLock);
    if (mNodeCeilings.empty()) {
        return;
    }

    for (const auto& [path, info] : mNodeCeilings) {
        std::string currentVal;
        if (!::android::base::ReadFileToString(path, &currentVal, true)) {
            continue;
        }
        currentVal = ::android::base::Trim(currentVal);
        char* end = nullptr;
        int64_t val = strtoll(currentVal.c_str(), &end, 10);
        if (end == currentVal.c_str() || *end != '\0') {
            continue;
        }

        int64_t clamped = val;
        if (info.maxCeiling > 0 && clamped > info.maxCeiling) {
            clamped = info.maxCeiling;
        }
        if (info.minFloor > 0 && clamped < info.minFloor) {
            clamped = info.minFloor;
        }

        if (clamped != val) {
            ::android::base::WriteStringToFile(std::to_string(clamped), path, true);
        }
    }
}

binder_status_t Power::dump(int fd, const char** /*args*/, uint32_t /*numArgs*/) {
    std::string buf = "QTI Power HAL AXKM Node Ceilings:\n";
    {
        std::lock_guard<std::mutex> lock(mCeilingLock);
        if (mNodeCeilings.empty()) {
            buf += "  No active node ceilings configured\n";
        } else {
            for (const auto& [path, info] : mNodeCeilings) {
                buf += "  Node: " + path + "\n";
                buf += "    Max Ceiling: " + std::to_string(info.maxCeiling) + "\n";
                buf += "    Min Floor: " + std::to_string(info.minFloor) + "\n";
                buf += "    Default Value: " + info.defaultValue + "\n";
            }
        }
    }
    ::android::base::WriteStringToFd(buf, fd);
    return STATUS_OK;
}

}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace android
}  // namespace aidl
