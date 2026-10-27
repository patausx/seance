#include "project_session.h"
#include "project_library.h"

namespace seance {

void ProjectSession::mark_mutated() {
    ++revision_;
    if (revision_ == 0) {
        // Keep zero out of normal state and preserve dirty semantics across the
        // practically unreachable 32-bit wrap.
        revision_ = 1;
        saved_revision_ = 0;
    }
}

void ProjectSession::mark_saved(int slot) {
    active_slot_ = project_slot_valid(slot) ? slot : -1;
    saved_revision_ = revision_;
}

void ProjectSession::reset_loaded(int slot) {
    active_slot_ = project_slot_valid(slot) ? slot : -1;
    ++revision_;
    if (revision_ == 0) revision_ = 1;
    saved_revision_ = revision_;
    pending_ = {};
}

void ProjectSession::reset_new() {
    active_slot_ = -1;
    ++revision_;
    if (revision_ == 0) revision_ = 1;
    saved_revision_ = revision_;
    pending_ = {};
}

void ProjectSession::detach_active_slot() {
    active_slot_ = -1;
    mark_mutated();
}

bool ProjectSession::request(DestructiveAction action, int slot) {
    if (action == DestructiveAction::None) return false;
    if (!dirty()) {
        pending_ = {};
        return true;
    }
    pending_ = {action, slot};
    return false;
}

PendingDestructiveAction ProjectSession::take_pending() {
    PendingDestructiveAction out = pending_;
    pending_ = {};
    return out;
}

} // namespace seance
