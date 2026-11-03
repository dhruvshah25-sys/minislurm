#pragma once
// TRES = "Trackable RESources". In Slurm every allocation is a vector of
// these (cpu, mem, gres/gpu, node, billing, ...). We start with the three
// that matter on spit-hpc.
#include <cstdint>

namespace msl {

struct Tres {
    std::uint32_t cpus   = 0;  // logical CPUs (hyperthreads) allocated
    std::uint64_t mem_mb = 0;  // memory in MB (Slurm's unit for RealMemory/--mem)
    std::uint32_t gpus   = 0;  // gres/gpu count
};

}  // namespace msl
