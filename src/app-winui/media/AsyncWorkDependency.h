#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winrt::Folia
{
    // Identifies one cache miss and the most recent completion for that key
    // observed while a visual block was prepared.  A later completion only
    // invalidates blocks that actually depend on the completed request.
    struct AsyncWorkDependency
    {
        std::string key;
        std::uint64_t observedCompletion = 0;

        explicit operator bool() const noexcept { return !key.empty(); }
    };

    using AsyncWorkDependencyGroup = std::vector<AsyncWorkDependency>;
}
