#include <jni.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

#include "util.h"
#include "../krnl_automata.hpp"

static_assert(sizeof(Match) == 8, "Java decoder requires 8-byte Match records");
static_assert(offsetof(Match, pos) == 0, "Unexpected Match.pos offset");
static_assert(offsetof(Match, ridPlusOne) == 4, "Unexpected Match.ridPlusOne offset");
static_assert(offsetof(Match, ragg_id) == 6, "Unexpected Match.ragg_id offset");
static_assert(offsetof(Match, valid) == 7, "Unexpected Match.valid offset");

namespace {

class UtfChars {
public:
    UtfChars(JNIEnv *env, jstring value)
        : env_(env), value_(value), chars_(env->GetStringUTFChars(value, nullptr)) {
        if (chars_ == nullptr) {
            throw std::runtime_error("Unable to read Java string");
        }
    }

    ~UtfChars() {
        env_->ReleaseStringUTFChars(value_, chars_);
    }

    const char *get() const {
        return chars_;
    }

private:
    JNIEnv *env_;
    jstring value_;
    const char *chars_;
};

void throwIOException(JNIEnv *env, const std::string &message) {
    jclass exceptionClass = env->FindClass("java/io/IOException");
    if (exceptionClass != nullptr) {
        env->ThrowNew(exceptionClass, message.c_str());
    }
}

std::size_t resultBytes(const MatchPack *output, std::size_t outputBytes) {
    std::size_t packCount = outputBytes / sizeof(MatchPack);
    for (std::size_t packIndex = 0; packIndex < packCount; packIndex++) {
        for (int matchIndex = 0; matchIndex < OUTPUT_PACKETSIZE; matchIndex++) {
            if (output[packIndex].matches[matchIndex].valid == 0) {
                return (packIndex * OUTPUT_PACKETSIZE + matchIndex + 1) * sizeof(Match);
            }
        }
    }
    throw std::runtime_error("FPGA output buffer does not contain a termination record");
}

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_PostAutoFFinder_FpgaNative_run(
        JNIEnv *env,
        jclass,
        jstring xclbinPath,
        jstring chromosomePath,
        jstring patternsPath,
        jint editDistance,
        jobject javaOutput) {
    try {
        if (javaOutput == nullptr) {
            throw std::invalid_argument("Output ByteBuffer is null");
        }
        void *javaOutputAddress = env->GetDirectBufferAddress(javaOutput);
        jlong javaOutputCapacity = env->GetDirectBufferCapacity(javaOutput);
        if (javaOutputAddress == nullptr || javaOutputCapacity <= 0) {
            throw std::invalid_argument("Output must be a non-empty direct ByteBuffer");
        }

        UtfChars xclbin(env, xclbinPath);
        UtfChars chromosome(env, chromosomePath);
        UtfChars patternsFile(env, patternsPath);

        std::vector<unsigned char> input = file2CharVector(chromosome.get());
        std::vector<std::vector<uint8_t>> patterns = read_patterns(patternsFile.get());
        if (patterns.size() < NUM_AUTOMATA) {
            throw std::invalid_argument("Pattern file contains fewer patterns than the FPGA image requires");
        }

        std::size_t patternBytes = 32 * NUM_AUTOMATA;
        std::size_t unpaddedBytes = input.size() + patternBytes;
        std::size_t paddedBytes = ((unpaddedBytes / IO_READ_BURST) + 1) * IO_READ_BURST;
        if (paddedBytes > static_cast<std::size_t>(javaOutputCapacity)) {
            throw std::invalid_argument("Direct ByteBuffer is smaller than the FPGA output buffer");
        }

        std::vector<char> inputBuffer(paddedBytes, 0);
        for (int patternIndex = 0; patternIndex < NUM_AUTOMATA; patternIndex++) {
            for (int symbolIndex = 0; symbolIndex < 23; symbolIndex++) {
                inputBuffer[patternIndex * 32 + symbolIndex] = patterns[patternIndex][symbolIndex];
            }
            inputBuffer[patternIndex * 32 + 31] = edit_distance_byte(editDistance);
        }
        std::memcpy(inputBuffer.data() + patternBytes, input.data(), input.size());

        xrt::device device(0);
        auto uuid = device.load_xclbin(xclbin.get());
        xrt::kernel kernel(device, uuid, "krnl_automata");
        xrt::bo inputBo(device, paddedBytes, kernel.group_id(0));
        xrt::bo outputBo(device, paddedBytes, kernel.group_id(1));

        inputBo.write(inputBuffer.data(), paddedBytes);
        inputBo.sync(XCL_BO_SYNC_BO_TO_DEVICE);

        auto run = kernel(inputBo, outputBo, static_cast<int>(paddedBytes));
        run.wait();
        outputBo.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

        MatchPack *mappedOutput = outputBo.map<MatchPack *>();
        std::size_t bytesWritten = resultBytes(mappedOutput, paddedBytes);
        std::memcpy(javaOutputAddress, mappedOutput, bytesWritten);
        return static_cast<jint>(bytesWritten);
    } catch (const std::exception &exception) {
        throwIOException(env, exception.what());
        return 0;
    }
}