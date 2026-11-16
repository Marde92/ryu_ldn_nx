#pragma once
#include <stratosphere.hpp>
namespace ryu_ldn::metadata {
struct GameMetadata {
    u64 application_id;
    u64 local_ids[8];
    char display_version[16];
};
void StartWorker();
void Request(u64 program_id);
bool WaitFor(u64 program_id, GameMetadata* result, s64 timeout_ms);
bool IsAllowed(const GameMetadata& metadata, u64 id);
}
