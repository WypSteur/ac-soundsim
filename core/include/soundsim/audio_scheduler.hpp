#pragma once
#include "win_mmf.hpp"
#include <avrt.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace soundsim {
// Deadline is the end of the just-published block. Keep the original time grid
// after a short late wake, rather than adding an extra whole idle period.
struct DeadlineRecovery { int missed{}; bool largeGap{}; };
inline DeadlineRecovery recoverAudioDeadline(double& deadline,double after,double period) {
    if (!std::isfinite(deadline) || !std::isfinite(after) || !std::isfinite(period) || period<=0)
        throw std::invalid_argument("invalid audio deadline");
    if (after<=deadline+period) return {};
    const double count=std::floor((after-deadline)/period);
    const int missed=static_cast<int>(std::min(150.0,count));
    const bool large=count>150;
    deadline=large ? after+period : deadline+missed*period;
    return {missed,large};
}

// No busy spin, device, global priority, power-plan or registry changes. Windows
// 10 1803+ high-resolution waitable timer is independent of Sleep granularity.
class AudioWakeTimer final {
public:
    AudioWakeTimer() {
        timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        if (!timer_) throw std::runtime_error("high-resolution audio timer unavailable: "+std::to_string(GetLastError()));
    }
    ~AudioWakeTimer() { CloseHandle(timer_); }
    AudioWakeTimer(const AudioWakeTimer&)=delete;
    AudioWakeTimer& operator=(const AudioWakeTimer&)=delete;
    bool wait(double delay,HANDLE stop) {
        if (!std::isfinite(delay) || delay>1) throw std::invalid_argument("invalid audio wait");
        if (delay<=0) return WaitForSingleObject(stop,0)!=WAIT_OBJECT_0;
        LARGE_INTEGER due{};
        due.QuadPart=-static_cast<LONGLONG>(std::max(1.0,std::ceil(delay*1e7)));
        if (!SetWaitableTimerEx(timer_,&due,0,nullptr,nullptr,nullptr,0))
            throw std::runtime_error("audio timer arm failed");
        const HANDLE handles[]{stop,timer_};
        const auto result=WaitForMultipleObjects(2,handles,FALSE,2000);
        if (result==WAIT_OBJECT_0) return false;
        if (result!=WAIT_OBJECT_0+1) throw std::runtime_error("audio timer wait failed");
        return true;
    }
private:
    HANDLE timer_{};
};

// Only the actively rendering thread joins MMCSS; idle/paused runtime releases
// it. Scoped registration is not REALTIME_PRIORITY_CLASS for the process.
class AudioThreadTask final {
public:
    AudioThreadTask()=default;
    ~AudioThreadTask() { setActive(false); }
    AudioThreadTask(const AudioThreadTask&)=delete;
    AudioThreadTask& operator=(const AudioThreadTask&)=delete;
    bool setActive(bool active) {
        if (!active) {
            if (task_) AvRevertMmThreadCharacteristics(task_);
            task_=nullptr; attempted_=false; return true;
        }
        if (!attempted_) {
            attempted_=true; DWORD index=0;
            task_=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
            if (!task_) error_=GetLastError();
        }
        return task_!=nullptr;
    }
    DWORD error() const { return error_; }
private:
    HANDLE task_{};
    bool attempted_{};
    DWORD error_{};
};
}
