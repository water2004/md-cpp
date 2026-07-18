// folia.platform.editor_priority_work_queue — deterministic visible-first work scheduling.
export module folia.platform.editor_priority_work_queue;
import std;

export namespace folia::platform::editor
{
    enum class EditorWorkPriority
    {
        Background,
        Visible,
    };

    enum class EditorQueueChange
    {
        Unchanged,
        Enqueued,
        Promoted,
    };

    // WorkItem owns a std::string `key`. The queue keeps at most one queued or
    // in-flight item per key, promotes background work without duplicating it,
    // and evicts background entries before visible ones at the capacity limit.
    // Completion uses a monotonic ticket so clearing and re-enqueuing the same
    // key cannot be undone by a stale in-flight completion.
    template<class WorkItem>
    class EditorPriorityWorkQueue
    {
    public:
        struct PoppedWork
        {
            WorkItem work;
            std::uint64_t ticket = 0;
            EditorWorkPriority priority = EditorWorkPriority::Background;
        };

        EditorQueueChange Enqueue(
            WorkItem work,
            EditorWorkPriority priority,
            std::size_t maximumQueued)
        {
            auto found = states_.find(work.key);
            if (found != states_.end())
            {
                if (priority == EditorWorkPriority::Visible
                    && found->second.status == Status::BackgroundQueued)
                {
                    auto queued = std::ranges::find(
                        background_,
                        work.key,
                        [](Entry const& entry) -> std::string const& {
                            return entry.work.key;
                        });
                    if (queued != background_.end())
                    {
                        visible_.push_back(std::move(*queued));
                        background_.erase(queued);
                        found->second.status = Status::VisibleQueued;
                        return EditorQueueChange::Promoted;
                    }
                }
                return EditorQueueChange::Unchanged;
            }
            if (maximumQueued == 0) return EditorQueueChange::Unchanged;

            while (QueuedSize() >= maximumQueued) EvictOne();
            auto ticket = nextTicket_++;
            states_.emplace(
                work.key,
                State{
                    priority == EditorWorkPriority::Visible
                        ? Status::VisibleQueued
                        : Status::BackgroundQueued,
                    ticket,
                });
            auto& queue = priority == EditorWorkPriority::Visible
                ? visible_
                : background_;
            queue.push_back(Entry{std::move(work), ticket});
            return EditorQueueChange::Enqueued;
        }

        bool Ready(bool allowBackground = true) const noexcept
        {
            return !visible_.empty() || (allowBackground && !background_.empty());
        }

        std::optional<PoppedWork> Pop(bool allowBackground = true)
        {
            auto priority = EditorWorkPriority::Visible;
            auto* queue = &visible_;
            if (queue->empty())
            {
                if (!allowBackground || background_.empty()) return std::nullopt;
                priority = EditorWorkPriority::Background;
                queue = &background_;
            }

            auto entry = std::move(queue->front());
            queue->pop_front();
            if (auto found = states_.find(entry.work.key);
                found != states_.end() && found->second.ticket == entry.ticket)
                found->second.status = Status::InFlight;
            return PoppedWork{
                std::move(entry.work),
                entry.ticket,
                priority,
            };
        }

        void Complete(std::string const& key, std::uint64_t ticket)
        {
            auto found = states_.find(key);
            if (found != states_.end() && found->second.ticket == ticket)
                states_.erase(found);
        }

        void Clear()
        {
            visible_.clear();
            background_.clear();
            states_.clear();
        }

        std::size_t QueuedSize() const noexcept
        {
            return visible_.size() + background_.size();
        }

    private:
        enum class Status
        {
            BackgroundQueued,
            VisibleQueued,
            InFlight,
        };

        struct Entry
        {
            WorkItem work;
            std::uint64_t ticket = 0;
        };

        struct State
        {
            Status status = Status::BackgroundQueued;
            std::uint64_t ticket = 0;
        };

        void EvictOne()
        {
            auto* queue = !background_.empty() ? &background_ : &visible_;
            if (queue->empty()) return;
            auto const& entry = queue->front();
            if (auto found = states_.find(entry.work.key);
                found != states_.end() && found->second.ticket == entry.ticket)
                states_.erase(found);
            queue->pop_front();
        }

        std::deque<Entry> visible_;
        std::deque<Entry> background_;
        std::unordered_map<std::string, State> states_;
        std::uint64_t nextTicket_ = 1;
    };
}
