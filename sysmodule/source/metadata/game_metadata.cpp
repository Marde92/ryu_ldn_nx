#include "game_metadata.hpp"
#include "../debug/log.hpp"
#include <switch/services/ns.h>
#include <cstring>

namespace ryu_ldn::metadata {
namespace {
ams::os::SdkMutex g_mutex;
ams::os::Event g_request_event(ams::os::EventClearMode_AutoClear);
ams::os::ThreadType g_thread;
alignas(ams::os::ThreadStackAlignment) u8 g_thread_stack[0x8000];
bool g_started = false;
u64 g_pending = 0;
u64 g_completed = 0;
bool g_success = false;
GameMetadata g_metadata{};
// Heap/static rather than thread stack: the returned struct contains a 128-KiB icon.
alignas(0x1000) NsApplicationControlData g_control_data{};

void Worker(void*) {
    for (;;) {
        g_request_event.Wait();
        u64 id;
        {
            std::scoped_lock lk(g_mutex);
            id = g_pending;
        }
        if (id == 0) continue;
        GameMetadata result{};
        result.application_id = id;
        ams::Result rc = nsInitialize();
        if (R_SUCCEEDED(rc)) {
            u64 actual_size = 0;
            rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, id,
                                             &g_control_data, sizeof(g_control_data),
                                             &actual_size);
            if (R_SUCCEEDED(rc) && actual_size >= sizeof(NacpStruct)) {
                std::memcpy(result.local_ids, g_control_data.nacp.local_communication_id,
                            sizeof(result.local_ids));
                std::memcpy(result.display_version, g_control_data.nacp.display_version,
                            sizeof(result.display_version));
            } else if (R_SUCCEEDED(rc)) {
                rc = MAKERESULT(0x10, 21);
            }
            nsExit();
        }
        const bool valid = R_SUCCEEDED(rc) && result.local_ids[0] != 0;
        {
            std::scoped_lock lk(g_mutex);
            // Don't publish stale data for a previous game.
            if (g_pending == id) {
                g_metadata = result;
                g_success = valid;
                g_completed = id;
            }
        }
        if (valid) {
            LOG_INFO("NACP_AUTO: app=0x%016llX local_id[0]=0x%016llX version='%.*s'",
                     static_cast<unsigned long long>(id),
                     static_cast<unsigned long long>(result.local_ids[0]),
                     16, result.display_version);
        } else {
            LOG_ERROR("NACP_AUTO: FAILED app=0x%016llX rc=0x%X (no hardcoded/title-ID fallback)",
                      static_cast<unsigned long long>(id), rc.GetValue());
        }
    }
}
}
void StartWorker() {
    std::scoped_lock lk(g_mutex);
    if (g_started) return;
    ams::Result rc = ams::os::CreateThread(&g_thread, Worker, nullptr,
                                          g_thread_stack, sizeof(g_thread_stack), 25);
    if (R_FAILED(rc)) {
        LOG_ERROR("NACP_AUTO: worker thread creation failed rc=0x%X", rc.GetValue());
        return;
    }
    ams::os::SetThreadNamePointer(&g_thread, "nacp_meta");
    ams::os::StartThread(&g_thread);
    g_started = true;
}
void Request(u64 program_id) {
    // Application IDs ignore the update-program suffix (0x800).
    const u64 app_id = program_id & ~0xFFFULL;
    {
        std::scoped_lock lk(g_mutex);
        if (g_pending == app_id) return;
        g_pending = app_id;
        g_completed = 0;
        g_success = false;
    }
    g_request_event.Signal();
}
bool WaitFor(u64 program_id, GameMetadata* result, s64 timeout_ms) {
    const u64 id = program_id & ~0xFFFULL;
    for (s64 ms = 0; ms <= timeout_ms; ms += 10) {
        {
            std::scoped_lock lk(g_mutex);
            if (g_completed == id) {
                if (g_success && result) *result = g_metadata;
                return g_success;
            }
        }
        if (ms < timeout_ms) svcSleepThread(10 * 1000000ULL);
    }
    LOG_ERROR("NACP_AUTO: timed out for application 0x%016llX; blocking lobby operation",
              static_cast<unsigned long long>(id));
    return false;
}
bool IsAllowed(const GameMetadata& m, u64 id) {
    for (u64 allowed : m.local_ids) if (allowed && allowed == id) return true;
    return false;
}
}
