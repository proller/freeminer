#pragma once

#include <functional>
#include <pthread.h>

using AsyncPayload = std::function<std::function<void()>()>;

struct MainLoop {
    MainLoop() = delete;
    static void NextFrame(std::function<void()> callback);
    static void DelayNextFrameUntilRedraw();
    static void RunAsyncThenResume(AsyncPayload payload);
};
