#include "support/folia_test.hpp"

import folia.platform.editor_priority_work_queue;

using namespace boost::ut;
using namespace folia::platform::editor;

namespace
{
    struct Work
    {
        std::string key;
        int value = 0;
    };
}

suite editor_priority_work_queue_tests = [] {
    "visible work overtakes queued background work"_test = [] {
        EditorPriorityWorkQueue<Work> queue;
        expect(queue.Enqueue({"background", 1}, EditorWorkPriority::Background, 8)
            == EditorQueueChange::Enqueued);
        expect(queue.Enqueue({"visible", 2}, EditorWorkPriority::Visible, 8)
            == EditorQueueChange::Enqueued);

        auto first = queue.Pop();
        auto second = queue.Pop();
        expect(first.has_value());
        expect(second.has_value());
        expect(first->work.key == "visible");
        expect(first->priority == EditorWorkPriority::Visible);
        expect(second->work.key == "background");
    };

    "visible request promotes the existing background item"_test = [] {
        EditorPriorityWorkQueue<Work> queue;
        queue.Enqueue({"same", 7}, EditorWorkPriority::Background, 8);
        queue.Enqueue({"other", 8}, EditorWorkPriority::Background, 8);
        expect(queue.Enqueue({"same", 99}, EditorWorkPriority::Visible, 8)
            == EditorQueueChange::Promoted);
        expect(queue.QueuedSize() == 2_u);

        auto promoted = queue.Pop();
        expect(promoted.has_value());
        expect(promoted->work.key == "same");
        expect(promoted->work.value == 7_i);
        expect(promoted->priority == EditorWorkPriority::Visible);
    };

    "capacity evicts background work before visible work"_test = [] {
        EditorPriorityWorkQueue<Work> queue;
        queue.Enqueue({"visible-1", 1}, EditorWorkPriority::Visible, 2);
        queue.Enqueue({"background", 2}, EditorWorkPriority::Background, 2);
        queue.Enqueue({"visible-2", 3}, EditorWorkPriority::Visible, 2);

        auto first = queue.Pop();
        auto second = queue.Pop();
        expect(first->work.key == "visible-1");
        expect(second->work.key == "visible-2");
        expect(!queue.Pop().has_value());
    };

    "stale completion cannot erase a re-enqueued key"_test = [] {
        EditorPriorityWorkQueue<Work> queue;
        queue.Enqueue({"same", 1}, EditorWorkPriority::Visible, 4);
        auto stale = queue.Pop();
        expect(stale.has_value());

        queue.Clear();
        queue.Enqueue({"same", 2}, EditorWorkPriority::Visible, 4);
        queue.Complete(stale->work.key, stale->ticket);
        expect(queue.Enqueue({"same", 3}, EditorWorkPriority::Visible, 4)
            == EditorQueueChange::Unchanged);
        auto current = queue.Pop();
        expect(current.has_value());
        expect(current->work.value == 2_i);
    };

    "background work can be paused without blocking visible work"_test = [] {
        EditorPriorityWorkQueue<Work> queue;
        queue.Enqueue({"background", 1}, EditorWorkPriority::Background, 4);
        expect(!queue.Pop(false).has_value());
        queue.Enqueue({"visible", 2}, EditorWorkPriority::Visible, 4);
        auto visible = queue.Pop(false);
        expect(visible.has_value());
        expect(visible->work.key == "visible");
        expect(!queue.Ready(false));
        expect(queue.Ready());
    };
};
