// project_session - persistent edit revision and destructive-action guard.
// Runtime transport/meters never touch this object; UI/controller command
// boundaries call mark_mutated() only for state serialized into a project.
#pragma once
#include <cstdint>

namespace seance {

enum class DestructiveAction : uint8_t { None = 0, NewProject, LoadProject, ExitApp };

struct PendingDestructiveAction {
    DestructiveAction action = DestructiveAction::None;
    int slot = -1;
};

class ProjectSession {
public:
    bool dirty() const { return revision_ != saved_revision_; }
    uint32_t revision() const { return revision_; }
    uint32_t saved_revision() const { return saved_revision_; }
    int active_slot() const { return active_slot_; }
    bool has_pending() const { return pending_.action != DestructiveAction::None; }
    PendingDestructiveAction pending() const { return pending_; }

    void mark_mutated();
    void mark_saved(int slot);
    void reset_loaded(int slot);
    void reset_new();
    void detach_active_slot();

    // Returns true when the action may execute immediately. Dirty projects arm
    // a pending action and return false so the platform can show confirmation.
    bool request(DestructiveAction action, int slot = -1);
    PendingDestructiveAction take_pending();
    void cancel_pending() { pending_ = {}; }

private:
    uint32_t revision_ = 1;
    uint32_t saved_revision_ = 1;
    int active_slot_ = -1;
    PendingDestructiveAction pending_;
};

} // namespace seance
