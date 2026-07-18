// folia.platform.editor_geometry — persistent block placement index.
export module folia.platform.editor_geometry;
import std;

export namespace folia::platform::editor
{
    class EditorBlockGeometryIndex
    {
    public:
        struct Entry
        {
            float marginTop = 0.0f;
            float height = 0.0f;
            float trailing = 0.0f;
        };

        struct Placement
        {
            float top = 0.0f;
            float bottom = 0.0f;
        };

        void Reset(std::vector<Entry> values, float documentPadding)
        {
            entries = std::move(values);
            padding = documentPadding;
            fenwick.assign(entries.size() + 1, 0.0f);
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                auto treeIndex = index + 1;
                fenwick[treeIndex] += Extent(entries[index]);
                auto parent = treeIndex + LowBit(treeIndex);
                if (parent < fenwick.size()) fenwick[parent] += fenwick[treeIndex];
            }
            initialized = true;
        }

        bool Initialized() const noexcept { return initialized; }
        std::size_t Size() const noexcept { return entries.size(); }

        Placement At(std::size_t index) const
        {
            if (index >= entries.size()) return {};
            auto const& entry = entries[index];
            auto top = padding + Prefix(index) + entry.marginTop;
            return {top, top + entry.height};
        }

        void UpdateHeight(std::size_t index, float height)
        {
            if (index >= entries.size()) return;
            auto updated = entries[index];
            updated.height = height;
            Update(index, updated);
        }

        void Update(std::size_t index, Entry updated)
        {
            if (index >= entries.size()) return;
            auto delta = Extent(updated) - Extent(entries[index]);
            entries[index] = updated;
            if (delta == 0.0f) return;
            for (auto treeIndex = index + 1; treeIndex < fenwick.size(); treeIndex += LowBit(treeIndex))
                fenwick[treeIndex] += delta;
        }

        std::size_t FirstIntersecting(float documentTop) const
        {
            std::size_t first = 0;
            auto last = entries.size();
            while (first < last)
            {
                auto middle = first + (last - first) / 2;
                if (At(middle).bottom < documentTop) first = middle + 1;
                else last = middle;
            }
            return first;
        }

        float TotalHeight() const
        {
            return padding + Prefix(entries.size()) + padding;
        }

    private:
        static std::size_t LowBit(std::size_t value)
        {
            return value & (~value + 1);
        }

        static float Extent(Entry const& entry)
        {
            return entry.marginTop + entry.height + entry.trailing;
        }

        float Prefix(std::size_t count) const
        {
            auto result = 0.0f;
            for (auto treeIndex = (std::min)(count, entries.size()); treeIndex > 0; treeIndex -= LowBit(treeIndex))
                result += fenwick[treeIndex];
            return result;
        }

        std::vector<Entry> entries;
        std::vector<float> fenwick;
        float padding = 0.0f;
        bool initialized = false;
    };
}
