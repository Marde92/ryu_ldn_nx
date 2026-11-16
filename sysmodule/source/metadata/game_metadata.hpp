#pragma once
#include <stratosphere.hpp>
namespace ryu_ldn::metadata {
struct GameMetadata {
    ams::u64 application_id;
    ams::u64 local_ids[8];
    char display_version[16];
};
void StartWorker();
void Request(ams::u64 program_id);
bool WaitFor(ams::u64 program_id, GameMetadata* result, ams::s64 timeout_ms);
bool IsAllowed(const GameMetadata& metadata, ams::u64 id);
}
