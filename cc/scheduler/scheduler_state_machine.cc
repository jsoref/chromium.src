// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/scheduler/scheduler_state_machine.h"

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "base/values.h"

namespace cc {

namespace {
// Surfaces and CompositorTimingHistory don't support more than 1 pending swap.
const int kMaxPendingSubmitFrames = 1;
}  // namespace

SchedulerStateMachine::SchedulerStateMachine(const SchedulerSettings& settings)
    : settings_(settings) {}

SchedulerStateMachine::~SchedulerStateMachine() = default;

const char* SchedulerStateMachine::LayerTreeFrameSinkStateToString(
    LayerTreeFrameSinkState state) {
  switch (state) {
    case LAYER_TREE_FRAME_SINK_NONE:
      return "LAYER_TREE_FRAME_SINK_NONE";
    case LAYER_TREE_FRAME_SINK_ACTIVE:
      return "LAYER_TREE_FRAME_SINK_ACTIVE";
    case LAYER_TREE_FRAME_SINK_CREATING:
      return "LAYER_TREE_FRAME_SINK_CREATING";
    case LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_COMMIT:
      return "LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_COMMIT";
    case LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_ACTIVATION:
      return "LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_ACTIVATION";
  }
  NOTREACHED();
  return "???";
}

const char* SchedulerStateMachine::BeginImplFrameStateToString(
    BeginImplFrameState state) {
  switch (state) {
    case BEGIN_IMPL_FRAME_STATE_IDLE:
      return "BEGIN_IMPL_FRAME_STATE_IDLE";
    case BEGIN_IMPL_FRAME_STATE_INSIDE_BEGIN_FRAME:
      return "BEGIN_IMPL_FRAME_STATE_INSIDE_BEGIN_FRAME";
    case BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE:
      return "BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE";
  }
  NOTREACHED();
  return "???";
}

const char* SchedulerStateMachine::BeginImplFrameDeadlineModeToString(
    BeginImplFrameDeadlineMode mode) {
  switch (mode) {
    case BEGIN_IMPL_FRAME_DEADLINE_MODE_NONE:
      return "BEGIN_IMPL_FRAME_DEADLINE_MODE_NONE";
    case BEGIN_IMPL_FRAME_DEADLINE_MODE_IMMEDIATE:
      return "BEGIN_IMPL_FRAME_DEADLINE_MODE_IMMEDIATE";
    case BEGIN_IMPL_FRAME_DEADLINE_MODE_REGULAR:
      return "BEGIN_IMPL_FRAME_DEADLINE_MODE_REGULAR";
    case BEGIN_IMPL_FRAME_DEADLINE_MODE_LATE:
      return "BEGIN_IMPL_FRAME_DEADLINE_MODE_LATE";
    case BEGIN_IMPL_FRAME_DEADLINE_MODE_BLOCKED:
      return "BEGIN_IMPL_FRAME_DEADLINE_MODE_BLOCKED";
  }
  NOTREACHED();
  return "???";
}

const char* SchedulerStateMachine::BeginMainFrameStateToString(
    BeginMainFrameState state) {
  switch (state) {
    case BEGIN_MAIN_FRAME_STATE_IDLE:
      return "BEGIN_MAIN_FRAME_STATE_IDLE";
    case BEGIN_MAIN_FRAME_STATE_SENT:
      return "BEGIN_MAIN_FRAME_STATE_SENT";
    case BEGIN_MAIN_FRAME_STATE_STARTED:
      return "BEGIN_MAIN_FRAME_STATE_STARTED";
    case BEGIN_MAIN_FRAME_STATE_READY_TO_COMMIT:
      return "BEGIN_MAIN_FRAME_STATE_READY_TO_COMMIT";
  }
  NOTREACHED();
  return "???";
}

const char* SchedulerStateMachine::ForcedRedrawOnTimeoutStateToString(
    ForcedRedrawOnTimeoutState state) {
  switch (state) {
    case FORCED_REDRAW_STATE_IDLE:
      return "FORCED_REDRAW_STATE_IDLE";
    case FORCED_REDRAW_STATE_WAITING_FOR_COMMIT:
      return "FORCED_REDRAW_STATE_WAITING_FOR_COMMIT";
    case FORCED_REDRAW_STATE_WAITING_FOR_ACTIVATION:
      return "FORCED_REDRAW_STATE_WAITING_FOR_ACTIVATION";
    case FORCED_REDRAW_STATE_WAITING_FOR_DRAW:
      return "FORCED_REDRAW_STATE_WAITING_FOR_DRAW";
  }
  NOTREACHED();
  return "???";
}

const char* ScrollHandlerStateToString(ScrollHandlerState state) {
  switch (state) {
    case ScrollHandlerState::SCROLL_AFFECTS_SCROLL_HANDLER:
      return "SCROLL_AFFECTS_SCROLL_HANDLER";
    case ScrollHandlerState::SCROLL_DOES_NOT_AFFECT_SCROLL_HANDLER:
      return "SCROLL_DOES_NOT_AFFECT_SCROLL_HANDLER";
  }
  NOTREACHED();
  return "???";
}

const char* SchedulerStateMachine::ActionToString(Action action) {
  switch (action) {
    case ACTION_NONE:
      return "ACTION_NONE";
    case ACTION_SEND_BEGIN_MAIN_FRAME:
      return "ACTION_SEND_BEGIN_MAIN_FRAME";
    case ACTION_COMMIT:
      return "ACTION_COMMIT";
    case ACTION_ACTIVATE_SYNC_TREE:
      return "ACTION_ACTIVATE_SYNC_TREE";
    case ACTION_DRAW_IF_POSSIBLE:
      return "ACTION_DRAW_IF_POSSIBLE";
    case ACTION_DRAW_FORCED:
      return "ACTION_DRAW_FORCED";
    case ACTION_DRAW_ABORT:
      return "ACTION_DRAW_ABORT";
    case ACTION_BEGIN_LAYER_TREE_FRAME_SINK_CREATION:
      return "ACTION_BEGIN_LAYER_TREE_FRAME_SINK_CREATION";
    case ACTION_PREPARE_TILES:
      return "ACTION_PREPARE_TILES";
    case ACTION_INVALIDATE_LAYER_TREE_FRAME_SINK:
      return "ACTION_INVALIDATE_LAYER_TREE_FRAME_SINK";
    case ACTION_PERFORM_IMPL_SIDE_INVALIDATION:
      return "ACTION_PERFORM_IMPL_SIDE_INVALIDATION";
    case ACTION_NOTIFY_BEGIN_MAIN_FRAME_NOT_SENT:
      return "ACTION_NOTIFY_BEGIN_MAIN_FRAME_NOT_SENT";
  }
  NOTREACHED();
  return "???";
}

std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
SchedulerStateMachine::AsValue() const {
  std::unique_ptr<base::trace_event::TracedValue> state(
      new base::trace_event::TracedValue());
  AsValueInto(state.get());
  return std::move(state);
}

void SchedulerStateMachine::AsValueInto(
    base::trace_event::TracedValue* state) const {
  state->BeginDictionary("major_state");
  state->SetString("next_action", ActionToString(NextAction()));
  state->SetString("begin_impl_frame_state",
                   BeginImplFrameStateToString(begin_impl_frame_state_));
  state->SetString("begin_main_frame_state",
                   BeginMainFrameStateToString(begin_main_frame_state_));
  state->SetString(
      "layer_tree_frame_sink_state",
      LayerTreeFrameSinkStateToString(layer_tree_frame_sink_state_));
  state->SetString("forced_redraw_state",
                   ForcedRedrawOnTimeoutStateToString(forced_redraw_state_));
  state->EndDictionary();

  state->BeginDictionary("minor_state");
  state->SetInteger("commit_count", commit_count_);
  state->SetInteger("current_frame_number", current_frame_number_);
  state->SetInteger("last_frame_number_submit_performed",
                    last_frame_number_submit_performed_);
  state->SetInteger("last_frame_number_draw_performed",
                    last_frame_number_draw_performed_);
  state->SetInteger("last_frame_number_begin_main_frame_sent",
                    last_frame_number_begin_main_frame_sent_);
  state->SetBoolean("did_draw", did_draw_);
  state->SetBoolean("did_send_begin_main_frame_for_current_frame",
                    did_send_begin_main_frame_for_current_frame_);
  state->SetBoolean("did_notify_begin_main_frame_not_sent",
                    did_notify_begin_main_frame_not_sent_);
  state->SetBoolean("wants_begin_main_frame_not_expected",
                    wants_begin_main_frame_not_expected_);
  state->SetBoolean("did_commit_during_frame", did_commit_during_frame_);
  state->SetBoolean("did_invalidate_layer_tree_frame_sink",
                    did_invalidate_layer_tree_frame_sink_);
  state->SetBoolean("did_perform_impl_side_invalidaion",
                    did_perform_impl_side_invalidation_);
  state->SetBoolean("did_prepare_tiles", did_prepare_tiles_);
  state->SetInteger("consecutive_checkerboard_animations",
                    consecutive_checkerboard_animations_);
  state->SetInteger("pending_submit_frames", pending_submit_frames_);
  state->SetInteger("submit_frames_with_current_layer_tree_frame_sink",
                    submit_frames_with_current_layer_tree_frame_sink_);
  state->SetBoolean("needs_redraw", needs_redraw_);
  state->SetBoolean("needs_prepare_tiles", needs_prepare_tiles_);
  state->SetBoolean("needs_begin_main_frame", needs_begin_main_frame_);
  state->SetBoolean("needs_one_begin_impl_frame", needs_one_begin_impl_frame_);
  state->SetBoolean("visible", visible_);
  state->SetBoolean("begin_frame_source_paused", begin_frame_source_paused_);
  state->SetBoolean("can_draw", can_draw_);
  state->SetBoolean("resourceless_draw", resourceless_draw_);
  state->SetBoolean("has_pending_tree", has_pending_tree_);
  state->SetBoolean("pending_tree_is_ready_for_activation",
                    pending_tree_is_ready_for_activation_);
  state->SetBoolean("active_tree_needs_first_draw",
                    active_tree_needs_first_draw_);
  state->SetBoolean("active_tree_is_ready_to_draw",
                    active_tree_is_ready_to_draw_);
  state->SetBoolean("did_create_and_initialize_first_layer_tree_frame_sink",
                    did_create_and_initialize_first_layer_tree_frame_sink_);
  state->SetString("tree_priority", TreePriorityToString(tree_priority_));
  state->SetString("scroll_handler_state",
                   ScrollHandlerStateToString(scroll_handler_state_));
  state->SetBoolean("critical_begin_main_frame_to_activate_is_fast",
                    critical_begin_main_frame_to_activate_is_fast_);
  state->SetBoolean("main_thread_missed_last_deadline",
                    main_thread_missed_last_deadline_);
  state->SetBoolean("skip_next_begin_main_frame_to_reduce_latency",
                    skip_next_begin_main_frame_to_reduce_latency_);
  state->SetBoolean("video_needs_begin_frames", video_needs_begin_frames_);
  state->SetBoolean("defer_commits", defer_commits_);
  state->SetBoolean("last_commit_had_no_updates", last_commit_had_no_updates_);
  state->SetBoolean("did_draw_in_last_frame", did_draw_in_last_frame_);
  state->SetBoolean("did_submit_in_last_frame", did_submit_in_last_frame_);
  state->SetBoolean("needs_impl_side_invalidation",
                    needs_impl_side_invalidation_);
  state->SetBoolean("current_pending_tree_is_impl_side",
                    current_pending_tree_is_impl_side_);
  state->SetBoolean("previous_pending_tree_was_impl_side",
                    previous_pending_tree_was_impl_side_);
  state->EndDictionary();
}

bool SchedulerStateMachine::PendingDrawsShouldBeAborted() const {
  // Normally when |visible_| is false or |begin_frame_source_paused_| is true,
  // pending activations will be forced and draws will be aborted. However,
  // when the embedder is Android WebView, software draws could be scheduled by
  // the Android OS at any time and draws should not be aborted in this case.
  bool is_layer_tree_frame_sink_lost =
      (layer_tree_frame_sink_state_ == LAYER_TREE_FRAME_SINK_NONE);
  if (resourceless_draw_)
    return is_layer_tree_frame_sink_lost || !can_draw_;

  // These are all the cases where we normally cannot or do not want
  // to draw but, if |needs_redraw_| is true and we do not draw to
  // make forward progress, we might deadlock with the main
  // thread. This should be a superset of ShouldAbortCurrentFrame()
  // since activation of the pending tree is blocked by drawing of the
  // active tree and the main thread might be blocked on activation of
  // the most recent commit.
  return is_layer_tree_frame_sink_lost || !can_draw_ || !visible_ ||
         begin_frame_source_paused_;
}

bool SchedulerStateMachine::ShouldAbortCurrentFrame() const {
  // Abort the frame if there is no output surface to trigger our
  // activations, avoiding deadlock with the main thread.
  if (layer_tree_frame_sink_state_ == LAYER_TREE_FRAME_SINK_NONE)
    return true;

  // If we're not visible, we should just abort the frame. Since we
  // set RequiresHighResToDraw when becoming visible, we ensure that
  // we don't checkerboard until all visible resources are
  // done. Furthermore, if we do keep the pending tree around, when
  // becoming visible we might activate prematurely causing
  // RequiresHighResToDraw flag to be reset. In all cases, we can
  // simply activate on becoming invisible since we don't need to draw
  // the active tree when we're in this state.
  if (!visible_)
    return true;

  // Abort the frame when viz::BeginFrameSource is paused to avoid
  // deadlocking the main thread.
  if (begin_frame_source_paused_)
    return true;

  return false;
}

bool SchedulerStateMachine::ShouldBeginLayerTreeFrameSinkCreation() const {
  if (!visible_)
    return false;

  // We only want to start output surface initialization after the
  // previous commit is complete.
  if (begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_IDLE) {
    return false;
  }

  // Make sure the BeginImplFrame from any previous LayerTreeFrameSinks
  // are complete before creating the new LayerTreeFrameSink.
  if (begin_impl_frame_state_ != BEGIN_IMPL_FRAME_STATE_IDLE)
    return false;

  // We want to clear the pipeline of any pending draws and activations
  // before starting output surface initialization. This allows us to avoid
  // weird corner cases where we abort draws or force activation while we
  // are initializing the output surface.
  if (active_tree_needs_first_draw_ || has_pending_tree_)
    return false;

  // We need to create the output surface if we don't have one and we haven't
  // started creating one yet.
  return layer_tree_frame_sink_state_ == LAYER_TREE_FRAME_SINK_NONE;
}

bool SchedulerStateMachine::ShouldDraw() const {
  // If we need to abort draws, we should do so ASAP since the draw could
  // be blocking other important actions (like output surface initialization),
  // from occurring. If we are waiting for the first draw, then perform the
  // aborted draw to keep things moving. If we are not waiting for the first
  // draw however, we don't want to abort for no reason.
  if (PendingDrawsShouldBeAborted())
    return active_tree_needs_first_draw_;

  // Do not draw more than once in the deadline. Aborted draws are ok because
  // those are effectively nops.
  if (did_draw_)
    return false;

  // Don't draw if we are waiting on the first commit after a surface.
  if (layer_tree_frame_sink_state_ != LAYER_TREE_FRAME_SINK_ACTIVE)
    return false;

  // Do not queue too many draws.
  if (IsDrawThrottled())
    return false;

  // Except for the cases above, do not draw outside of the BeginImplFrame
  // deadline.
  if (begin_impl_frame_state_ != BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE)
    return false;

  // Wait for ready to draw in full-pipeline mode or the browser compositor's
  // commit-to-active-tree mode.
  if ((settings_.wait_for_all_pipeline_stages_before_draw ||
       settings_.commit_to_active_tree) &&
      !active_tree_is_ready_to_draw_) {
    return false;
  }

  // Browser compositor commit steals any resources submitted in draw. Therefore
  // drawing while a commit is pending is wasteful.
  if (settings_.commit_to_active_tree && CommitPending())
    return false;

  // Only handle forced redraws due to timeouts on the regular deadline.
  if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_DRAW)
    return true;

  return needs_redraw_;
}

bool SchedulerStateMachine::ShouldActivateSyncTree() const {
  // There is nothing to activate.
  if (!has_pending_tree_)
    return false;

  // We should not activate a second tree before drawing the first one.
  // Even if we need to force activation of the pending tree, we should abort
  // drawing the active tree first.
  if (active_tree_needs_first_draw_)
    return false;

  if (ShouldAbortCurrentFrame())
    return true;

  // At this point, only activate if we are ready to activate.
  return pending_tree_is_ready_for_activation_;
}

bool SchedulerStateMachine::ShouldNotifyBeginMainFrameNotSent() const {
  // This method returns true if most of the conditions for sending a
  // BeginMainFrame are met, but one is not actually requested. This gives the
  // main thread the chance to do something else.

  if (!wants_begin_main_frame_not_expected_)
    return false;

  // Don't notify if a BeginMainFrame has already been requested or is in
  // progress.
  if (needs_begin_main_frame_ ||
      begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_IDLE)
    return false;

  // Only notify when we're visible.
  if (!visible_)
    return false;

  // There are no BeginImplFrames while viz::BeginFrameSource is paused, meaning
  // the scheduler should send SendBeginMainFrameNotExpectedSoon instead,
  // indicating a longer period of inactivity.
  if (begin_frame_source_paused_)
    return false;

  // Do not notify that no BeginMainFrame was sent too many times in a single
  // frame.
  if (did_notify_begin_main_frame_not_sent_)
    return false;

  // Do not notify if a commit happened during this frame as the main thread
  // will already be active and does not need to be woken up to make further
  // actions. (This occurs if the main frame was scheduled but didn't complete
  // before the vsync deadline).
  if (did_commit_during_frame_)
    return false;

  return true;
}

bool SchedulerStateMachine::CouldSendBeginMainFrame() const {
  if (!needs_begin_main_frame_)
    return false;

  // We can not perform commits if we are not visible.
  if (!visible_)
    return false;

  // There are no BeginImplFrames while viz::BeginFrameSource is paused,
  // so should also stop BeginMainFrames.
  if (begin_frame_source_paused_)
    return false;

  // Do not make a new commits when it is deferred.
  if (defer_commits_)
    return false;

  return true;
}

bool SchedulerStateMachine::ShouldSendBeginMainFrame() const {
  if (!CouldSendBeginMainFrame())
    return false;

  // Do not send more than one begin main frame in a begin frame.
  if (did_send_begin_main_frame_for_current_frame_)
    return false;

  // Only send BeginMainFrame when there isn't another commit pending already.
  // Other parts of the state machine indirectly defer the BeginMainFrame
  // by transitioning to WAITING commit states rather than going
  // immediately to IDLE.
  if (begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_IDLE)
    return false;

  // MFBA is disabled and we are waiting for previous activation.
  if (!settings_.main_frame_before_activation_enabled && has_pending_tree_)
    return false;

  // We are waiting for previous frame to be drawn, submitted and acked.
  if (settings_.commit_to_active_tree &&
      (active_tree_needs_first_draw_ || IsDrawThrottled())) {
    return false;
  }

  // Don't send BeginMainFrame early if we are prioritizing the active tree
  // because of ImplLatencyTakesPriority.
  if (ImplLatencyTakesPriority() &&
      (has_pending_tree_ || active_tree_needs_first_draw_)) {
    return false;
  }

  // We should not send BeginMainFrame while we are in the idle state since we
  // might have new user input arriving soon. It's okay to send BeginMainFrame
  // for the synchronous compositor because the main thread is always high
  // latency in that case.
  // TODO(brianderson): Allow sending BeginMainFrame while idle when the main
  // thread isn't consuming user input for non-synchronous compositor.
  if (!settings_.using_synchronous_renderer_compositor &&
      begin_impl_frame_state_ == BEGIN_IMPL_FRAME_STATE_IDLE) {
    return false;
  }

  // We need a new commit for the forced redraw. This honors the
  // single commit per interval because the result will be swapped to screen.
  if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_COMMIT)
    return true;

  // We shouldn't normally accept commits if there isn't a LayerTreeFrameSink.
  if (!HasInitializedLayerTreeFrameSink())
    return false;

  if (!settings_.main_frame_while_submit_frame_throttled_enabled) {
    // Throttle the BeginMainFrames on CompositorFrameAck unless we just
    // submitted a frame to potentially improve impl-thread latency over
    // main-thread throughput.
    // TODO(brianderson): Remove this restriction to improve throughput or
    // make it conditional on ImplLatencyTakesPriority.
    bool just_submitted_in_deadline =
        begin_impl_frame_state_ == BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE &&
        did_submit_in_last_frame_;
    if (IsDrawThrottled() && !just_submitted_in_deadline)
      return false;
  }

  if (skip_next_begin_main_frame_to_reduce_latency_)
    return false;

  return true;
}

bool SchedulerStateMachine::ShouldCommit() const {
  if (begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_READY_TO_COMMIT)
    return false;

  // We must not finish the commit until the pending tree is free.
  if (has_pending_tree_) {
    DCHECK(settings_.main_frame_before_activation_enabled ||
           current_pending_tree_is_impl_side_);
    return false;
  }

  // If we only have an active tree, it is incorrect to replace it before we've
  // drawn it.
  DCHECK(!settings_.commit_to_active_tree || !active_tree_needs_first_draw_);

  // In browser compositor commit reclaims any resources submitted during draw.
  DCHECK(!settings_.commit_to_active_tree || !IsDrawThrottled());

  return true;
}

bool SchedulerStateMachine::ShouldPrepareTiles() const {
  // In full-pipeline mode, we need to prepare tiles ASAP to ensure that we
  // don't get stuck.
  if (settings_.wait_for_all_pipeline_stages_before_draw)
    return needs_prepare_tiles_;

  // Do not prepare tiles if we've already done so in commit or impl side
  // invalidation.
  if (did_prepare_tiles_)
    return false;

  // Limiting to once per-frame is not enough, since we only want to prepare
  // tiles _after_ draws.
  if (begin_impl_frame_state_ != BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE)
    return false;

  return needs_prepare_tiles_;
}

bool SchedulerStateMachine::ShouldInvalidateLayerTreeFrameSink() const {
  // Do not invalidate more than once per begin frame.
  if (did_invalidate_layer_tree_frame_sink_)
    return false;

  // Only the synchronous compositor requires invalidations.
  if (!settings_.using_synchronous_renderer_compositor)
    return false;

  // Invalidations are only performed inside a BeginFrame.
  if (begin_impl_frame_state_ != BEGIN_IMPL_FRAME_STATE_INSIDE_BEGIN_FRAME)
    return false;

  // TODO(sunnyps): needs_prepare_tiles_ is needed here because PrepareTiles is
  // called only inside the deadline / draw phase. We could remove this if we
  // allowed PrepareTiles to happen in OnBeginImplFrame.
  return needs_redraw_ || needs_prepare_tiles_;
}

SchedulerStateMachine::Action SchedulerStateMachine::NextAction() const {
  if (ShouldActivateSyncTree())
    return ACTION_ACTIVATE_SYNC_TREE;
  if (ShouldCommit())
    return ACTION_COMMIT;
  if (ShouldDraw()) {
    if (PendingDrawsShouldBeAborted())
      return ACTION_DRAW_ABORT;
    else if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_DRAW)
      return ACTION_DRAW_FORCED;
    else
      return ACTION_DRAW_IF_POSSIBLE;
  }
  if (ShouldPerformImplSideInvalidation())
    return ACTION_PERFORM_IMPL_SIDE_INVALIDATION;
  if (ShouldPrepareTiles())
    return ACTION_PREPARE_TILES;
  if (ShouldSendBeginMainFrame())
    return ACTION_SEND_BEGIN_MAIN_FRAME;
  if (ShouldInvalidateLayerTreeFrameSink())
    return ACTION_INVALIDATE_LAYER_TREE_FRAME_SINK;
  if (ShouldBeginLayerTreeFrameSinkCreation())
    return ACTION_BEGIN_LAYER_TREE_FRAME_SINK_CREATION;
  if (ShouldNotifyBeginMainFrameNotSent())
    return ACTION_NOTIFY_BEGIN_MAIN_FRAME_NOT_SENT;
  return ACTION_NONE;
}

bool SchedulerStateMachine::ShouldPerformImplSideInvalidation() const {
  if (!needs_impl_side_invalidation_)
    return false;

  // Only perform impl side invalidation after the frame ends so that we wait
  // for any commit to happen before invalidating.
  // TODO(khushalsagar): Invalidate at the beginning of the frame if there is no
  // commit request from the main thread.
  if (begin_impl_frame_state_ != BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE)
    return false;

  if (!CouldCreatePendingTree())
    return false;

  // If the main thread is ready to commit, the impl-side invalidations will be
  // merged with the incoming main frame.
  if (begin_main_frame_state_ == BEGIN_MAIN_FRAME_STATE_READY_TO_COMMIT)
    return false;

  // Don't invalidate if we've already done so either from the scheduler or as
  // part of commit.
  if (did_perform_impl_side_invalidation_)
    return false;

  // If invalidations go to the active tree and we are waiting for the previous
  // frame to be drawn, submitted and acked.
  if (settings_.commit_to_active_tree &&
      (active_tree_needs_first_draw_ || IsDrawThrottled())) {
    return false;
  }

  return true;
}

void SchedulerStateMachine::WillPerformImplSideInvalidation() {
  current_pending_tree_is_impl_side_ = true;
  WillPerformImplSideInvalidationInternal();
}

void SchedulerStateMachine::WillPerformImplSideInvalidationInternal() {
  DCHECK(needs_impl_side_invalidation_);
  DCHECK(!has_pending_tree_);

  needs_impl_side_invalidation_ = false;
  has_pending_tree_ = true;
  did_perform_impl_side_invalidation_ = true;
  pending_tree_needs_first_draw_on_activation_ =
      next_invalidation_needs_first_draw_on_activation_;
  next_invalidation_needs_first_draw_on_activation_ = false;
  // TODO(eseckler): Track impl-side invalidations for pending/active tree and
  // CompositorFrame freshness computation.
}

bool SchedulerStateMachine::CouldCreatePendingTree() const {
  // Can't create a new pending tree till the current one is activated.
  if (has_pending_tree_)
    return false;

  // Can't make frames while we're invisible.
  if (!visible_)
    return false;

  // If the viz::BeginFrameSource is paused, we will not be able to make any
  // impl frames.
  if (begin_frame_source_paused_)
    return false;

  // Don't create a pending tree till a frame sink is initialized.
  if (!HasInitializedLayerTreeFrameSink())
    return false;

  return true;
}

void SchedulerStateMachine::WillSendBeginMainFrame() {
  DCHECK(!has_pending_tree_ || settings_.main_frame_before_activation_enabled);
  DCHECK(visible_);
  DCHECK(!begin_frame_source_paused_);
  DCHECK(!did_send_begin_main_frame_for_current_frame_);
  begin_main_frame_state_ = BEGIN_MAIN_FRAME_STATE_SENT;
  needs_begin_main_frame_ = false;
  did_send_begin_main_frame_for_current_frame_ = true;
  last_frame_number_begin_main_frame_sent_ = current_frame_number_;
}

void SchedulerStateMachine::WillNotifyBeginMainFrameNotSent() {
  DCHECK(visible_);
  DCHECK(!begin_frame_source_paused_);
  DCHECK(!did_notify_begin_main_frame_not_sent_);
  did_notify_begin_main_frame_not_sent_ = true;
}

void SchedulerStateMachine::WillCommit(bool commit_has_no_updates) {
  bool can_have_pending_tree =
      commit_has_no_updates &&
      (settings_.main_frame_before_activation_enabled ||
       current_pending_tree_is_impl_side_);
  DCHECK(!has_pending_tree_ || can_have_pending_tree);
  commit_count_++;
  last_commit_had_no_updates_ = commit_has_no_updates;
  begin_main_frame_state_ = BEGIN_MAIN_FRAME_STATE_IDLE;
  did_commit_during_frame_ = true;

  if (!commit_has_no_updates) {
    // If there was a commit, the impl-side invalidations will be merged with
    // it. We always fill the impl-side invalidation funnel here, even if no
    // request was currently pending, to defer creating another pending tree and
    // performing PrepareTiles until the next frame, in case the invalidation
    // request is received after the commit.
    if (needs_impl_side_invalidation_)
      WillPerformImplSideInvalidationInternal();
    did_perform_impl_side_invalidation_ = true;

    // We have a new pending tree.
    has_pending_tree_ = true;
    pending_tree_needs_first_draw_on_activation_ = true;
    pending_tree_is_ready_for_activation_ = false;
    // Wait for the new pending tree to become ready to draw, which may happen
    // before or after activation.
    active_tree_is_ready_to_draw_ = false;
  }

  // Update state related to forced draws.
  if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_COMMIT) {
    forced_redraw_state_ = has_pending_tree_
                               ? FORCED_REDRAW_STATE_WAITING_FOR_ACTIVATION
                               : FORCED_REDRAW_STATE_WAITING_FOR_DRAW;
  }

  // Update the output surface state.
  if (layer_tree_frame_sink_state_ ==
      LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_COMMIT) {
    layer_tree_frame_sink_state_ =
        has_pending_tree_ ? LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_ACTIVATION
                          : LAYER_TREE_FRAME_SINK_ACTIVE;
  }
}

void SchedulerStateMachine::WillActivate() {
  if (layer_tree_frame_sink_state_ ==
      LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_ACTIVATION)
    layer_tree_frame_sink_state_ = LAYER_TREE_FRAME_SINK_ACTIVE;

  if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_ACTIVATION)
    forced_redraw_state_ = FORCED_REDRAW_STATE_WAITING_FOR_DRAW;

  has_pending_tree_ = false;
  pending_tree_is_ready_for_activation_ = false;
  active_tree_needs_first_draw_ = pending_tree_needs_first_draw_on_activation_;
  pending_tree_needs_first_draw_on_activation_ = false;
  needs_redraw_ = true;

  previous_pending_tree_was_impl_side_ = current_pending_tree_is_impl_side_;
  current_pending_tree_is_impl_side_ = false;
}

void SchedulerStateMachine::WillDrawInternal() {
  // If a new active tree is pending after the one we are about to draw,
  // the main thread is in a high latency mode.
  // main_thread_missed_last_deadline_ is here in addition to
  // OnBeginImplFrameIdle for cases where the scheduler aborts draws outside
  // of the deadline.
  main_thread_missed_last_deadline_ = CommitPending() || has_pending_tree_;

  // We need to reset needs_redraw_ before we draw since the
  // draw itself might request another draw.
  needs_redraw_ = false;

  did_draw_ = true;
  active_tree_needs_first_draw_ = false;
  did_draw_in_last_frame_ = true;
  last_frame_number_draw_performed_ = current_frame_number_;

  if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_DRAW)
    forced_redraw_state_ = FORCED_REDRAW_STATE_IDLE;
}

void SchedulerStateMachine::DidDrawInternal(DrawResult draw_result) {
  switch (draw_result) {
    case INVALID_RESULT:
    case DRAW_ABORTED_CANT_DRAW:
      NOTREACHED() << "Invalid return DrawResult:" << draw_result;
      break;
    case DRAW_ABORTED_DRAINING_PIPELINE:
    case DRAW_SUCCESS:
      consecutive_checkerboard_animations_ = 0;
      forced_redraw_state_ = FORCED_REDRAW_STATE_IDLE;
      break;
    case DRAW_ABORTED_CHECKERBOARD_ANIMATIONS:
      DCHECK(!did_submit_in_last_frame_);
      needs_begin_main_frame_ = true;
      needs_redraw_ = true;
      consecutive_checkerboard_animations_++;

      if (consecutive_checkerboard_animations_ >=
              settings_.maximum_number_of_failed_draws_before_draw_is_forced &&
          forced_redraw_state_ == FORCED_REDRAW_STATE_IDLE &&
          settings_.timeout_and_draw_when_animation_checkerboards) {
        // We need to force a draw, but it doesn't make sense to do this until
        // we've committed and have new textures.
        forced_redraw_state_ = FORCED_REDRAW_STATE_WAITING_FOR_COMMIT;
      }
      break;
    case DRAW_ABORTED_MISSING_HIGH_RES_CONTENT:
      DCHECK(!did_submit_in_last_frame_);
      // It's not clear whether this missing content is because of missing
      // pictures (which requires a commit) or because of memory pressure
      // removing textures (which might not).  To be safe, request a commit
      // anyway.
      needs_begin_main_frame_ = true;
      break;
  }
}

void SchedulerStateMachine::WillDraw() {
  DCHECK(!did_draw_);
  WillDrawInternal();
}

void SchedulerStateMachine::DidDraw(DrawResult draw_result) {
  DidDrawInternal(draw_result);
}

void SchedulerStateMachine::SetNeedsImplSideInvalidation(
    bool needs_first_draw_on_activation) {
  needs_impl_side_invalidation_ = true;
  next_invalidation_needs_first_draw_on_activation_ |=
      needs_first_draw_on_activation;
}

void SchedulerStateMachine::SetMainThreadWantsBeginMainFrameNotExpectedMessages(
    bool new_state) {
  wants_begin_main_frame_not_expected_ = new_state;
}

void SchedulerStateMachine::AbortDraw() {
  // Pretend like the draw was successful.
  // Note: We may abort at any time and cannot DCHECK that
  // we haven't drawn in or swapped in the last frame here.
  WillDrawInternal();
  DidDrawInternal(DRAW_ABORTED_DRAINING_PIPELINE);
}

void SchedulerStateMachine::WillPrepareTiles() {
  needs_prepare_tiles_ = false;
}

void SchedulerStateMachine::WillBeginLayerTreeFrameSinkCreation() {
  DCHECK_EQ(layer_tree_frame_sink_state_, LAYER_TREE_FRAME_SINK_NONE);
  layer_tree_frame_sink_state_ = LAYER_TREE_FRAME_SINK_CREATING;

  // The following DCHECKs make sure we are in the proper quiescent state.
  // The pipeline should be flushed entirely before we start output
  // surface creation to avoid complicated corner cases.
  DCHECK(begin_main_frame_state_ == BEGIN_MAIN_FRAME_STATE_IDLE);
  DCHECK(!has_pending_tree_);
  DCHECK(!active_tree_needs_first_draw_);
}

void SchedulerStateMachine::WillInvalidateLayerTreeFrameSink() {
  DCHECK(!did_invalidate_layer_tree_frame_sink_);
  did_invalidate_layer_tree_frame_sink_ = true;
  last_frame_number_invalidate_layer_tree_frame_sink_performed_ =
      current_frame_number_;

  // The synchronous compositor makes no guarantees about a draw coming in after
  // an invalidate so clear any flags that would cause the compositor's pipeline
  // to stall.
  active_tree_needs_first_draw_ = false;  // blocks commit if true
}

void SchedulerStateMachine::SetSkipNextBeginMainFrameToReduceLatency() {
  TRACE_EVENT_INSTANT0("cc", "Scheduler: SkipNextBeginMainFrameToReduceLatency",
                       TRACE_EVENT_SCOPE_THREAD);
  skip_next_begin_main_frame_to_reduce_latency_ = true;
}

bool SchedulerStateMachine::BeginFrameNeededForVideo() const {
  return video_needs_begin_frames_;
}

bool SchedulerStateMachine::BeginFrameNeeded() const {
  // We can't handle BeginFrames when output surface isn't initialized.
  // TODO(brianderson): Support output surface creation inside a BeginFrame.
  if (!HasInitializedLayerTreeFrameSink())
    return false;

  // If we are not visible, we don't need BeginFrame messages.
  if (!visible_)
    return false;

  return BeginFrameRequiredForAction() || BeginFrameNeededForVideo() ||
         ProactiveBeginFrameWanted();
}

void SchedulerStateMachine::SetVideoNeedsBeginFrames(
    bool video_needs_begin_frames) {
  video_needs_begin_frames_ = video_needs_begin_frames;
}

void SchedulerStateMachine::SetDeferCommits(bool defer_commits) {
  defer_commits_ = defer_commits;
}

// These are the cases where we require a BeginFrame message to make progress
// on requested actions.
bool SchedulerStateMachine::BeginFrameRequiredForAction() const {
  // The forced draw respects our normal draw scheduling, so we need to
  // request a BeginImplFrame for it.
  if (forced_redraw_state_ == FORCED_REDRAW_STATE_WAITING_FOR_DRAW)
    return true;

  return needs_redraw_ || needs_one_begin_impl_frame_ ||
         (needs_begin_main_frame_ && !defer_commits_) ||
         needs_impl_side_invalidation_;
}

// These are cases where we are very likely want a BeginFrame message in the
// near future. Proactively requesting the BeginImplFrame helps hide the round
// trip latency of the SetNeedsBeginFrame request that has to go to the
// Browser.
// This includes things like drawing soon, but might not actually have a new
// frame to draw when we receive the next BeginImplFrame.
bool SchedulerStateMachine::ProactiveBeginFrameWanted() const {
  // Do not be proactive when invisible.
  if (!visible_)
    return false;

  // We should proactively request a BeginImplFrame if a commit is pending
  // because we will want to draw if the commit completes quickly. Do not
  // request frames when commits are disabled, because the frame requests will
  // not provide the needed commit (and will wake up the process when it could
  // stay idle).
  if ((begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_IDLE) &&
      !defer_commits_)
    return true;

  // If the pending tree activates quickly, we'll want a BeginImplFrame soon
  // to draw the new active tree.
  if (has_pending_tree_)
    return true;

  // Changing priorities may allow us to activate (given the new priorities),
  // which may result in a new frame.
  if (needs_prepare_tiles_)
    return true;

  // If we just tried to draw, it's likely that we are going to produce another
  // frame soon. This helps avoid negative glitches in our SetNeedsBeginFrame
  // requests, which may propagate to the BeginImplFrame provider and get
  // sampled at an inopportune time, delaying the next BeginImplFrame.
  if (did_draw_in_last_frame_)
    return true;

  // If the last commit was aborted because of early out (no updates), we should
  // still want a begin frame in case there is a commit coming again.
  if (last_commit_had_no_updates_)
    return true;

  return false;
}

void SchedulerStateMachine::OnBeginImplFrame(uint32_t source_id,
                                             uint64_t sequence_number) {
  begin_impl_frame_state_ = BEGIN_IMPL_FRAME_STATE_INSIDE_BEGIN_FRAME;
  current_frame_number_++;

  last_commit_had_no_updates_ = false;
  did_draw_in_last_frame_ = false;
  did_submit_in_last_frame_ = false;
  needs_one_begin_impl_frame_ = false;

  did_notify_begin_main_frame_not_sent_ = false;
  did_send_begin_main_frame_for_current_frame_ = false;
  did_commit_during_frame_ = false;
  did_invalidate_layer_tree_frame_sink_ = false;
  did_perform_impl_side_invalidation_ = false;
}

void SchedulerStateMachine::OnBeginImplFrameDeadline() {
  begin_impl_frame_state_ = BEGIN_IMPL_FRAME_STATE_INSIDE_DEADLINE;

  // Clear funnels for any actions we perform during the deadline.
  did_draw_ = false;
}

void SchedulerStateMachine::OnBeginImplFrameIdle() {
  begin_impl_frame_state_ = BEGIN_IMPL_FRAME_STATE_IDLE;

  // Count any prepare tiles that happens in commits in between frames. We want
  // to prevent a prepare tiles during the next frame's deadline in that case.
  // This also allows synchronous compositor to do one PrepareTiles per draw.
  // This is same as the old prepare tiles funnel behavior.
  did_prepare_tiles_ = false;

  skip_next_begin_main_frame_to_reduce_latency_ = false;

  // If a new or undrawn active tree is pending after the deadline,
  // then the main thread is in a high latency mode.
  main_thread_missed_last_deadline_ =
      CommitPending() || has_pending_tree_ || active_tree_needs_first_draw_;

  // If we're entering a state where we won't get BeginFrames set all the
  // funnels so that we don't perform any actions that we shouldn't.
  if (!BeginFrameNeeded())
    did_send_begin_main_frame_for_current_frame_ = true;
}

SchedulerStateMachine::BeginImplFrameDeadlineMode
SchedulerStateMachine::CurrentBeginImplFrameDeadlineMode() const {
  if (settings_.using_synchronous_renderer_compositor) {
    // No deadline for synchronous compositor.
    return BEGIN_IMPL_FRAME_DEADLINE_MODE_NONE;
  } else if (ShouldBlockDeadlineIndefinitely()) {
    return BEGIN_IMPL_FRAME_DEADLINE_MODE_BLOCKED;
  } else if (ShouldTriggerBeginImplFrameDeadlineImmediately()) {
    return BEGIN_IMPL_FRAME_DEADLINE_MODE_IMMEDIATE;
  } else if (needs_redraw_) {
    // We have an animation or fast input path on the impl thread that wants
    // to draw, so don't wait too long for a new active tree.
    return BEGIN_IMPL_FRAME_DEADLINE_MODE_REGULAR;
  } else {
    // The impl thread doesn't have anything it wants to draw and we are just
    // waiting for a new active tree. In short we are blocked.
    return BEGIN_IMPL_FRAME_DEADLINE_MODE_LATE;
  }
}

bool SchedulerStateMachine::ShouldTriggerBeginImplFrameDeadlineImmediately()
    const {
  // If we aborted the current frame we should end the deadline right now.
  if (ShouldAbortCurrentFrame() && !has_pending_tree_)
    return true;

  // Throttle the deadline on CompositorFrameAck since we wont draw and submit
  // anyway.
  if (IsDrawThrottled())
    return false;

  // In full-pipe mode, we just gave all pipeline stages a chance to contribute.
  // We shouldn't wait any longer in any case - even if there are no updates.
  if (settings_.wait_for_all_pipeline_stages_before_draw)
    return true;

  if (active_tree_needs_first_draw_)
    return true;

  if (!needs_redraw_)
    return false;

  // This is used to prioritize impl-thread draws when the main thread isn't
  // producing anything, e.g., after an aborted commit. We also check that we
  // don't have a pending tree -- otherwise we should give it a chance to
  // activate.
  // TODO(skyostil): Revisit this when we have more accurate deadline estimates.
  if (!CommitPending() && !has_pending_tree_)
    return true;

  // Prioritize impl-thread draws in ImplLatencyTakesPriority mode.
  if (ImplLatencyTakesPriority())
    return true;

  return false;
}

bool SchedulerStateMachine::ShouldBlockDeadlineIndefinitely() const {
  if (!settings_.wait_for_all_pipeline_stages_before_draw &&
      !settings_.commit_to_active_tree) {
    return false;
  }

  // Avoid blocking for any reason if we don't have a layer tree frame sink or
  // are invisible.
  if (layer_tree_frame_sink_state_ == LAYER_TREE_FRAME_SINK_NONE)
    return false;

  if (!visible_)
    return false;

  // Wait for main frame to be ready for commits if in full-pipe mode, so that
  // we ensure we block during renderer initialization. In commit_to_active_tree
  // mode, we cannot block for defer_commits_, as this may negatively affect
  // animation smoothness during resize or orientation changes.
  if (defer_commits_ && settings_.wait_for_all_pipeline_stages_before_draw)
    return true;

  // Wait for main frame if one is in progress or about to be started.
  if (ShouldSendBeginMainFrame())
    return true;

  if (begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_IDLE)
    return true;

  // Wait for tiles and activation.
  if (has_pending_tree_)
    return true;

  // Avoid blocking for draw when we can't draw. We block in the above cases
  // even if we cannot draw, because we may still be waiting for the first
  // active tree.
  if (!can_draw_)
    return false;

  // Wait for remaining tiles and draw.
  if (!active_tree_is_ready_to_draw_)
    return true;

  return false;
}

bool SchedulerStateMachine::IsDrawThrottled() const {
  return pending_submit_frames_ >= kMaxPendingSubmitFrames;
}

void SchedulerStateMachine::SetVisible(bool visible) {
  if (visible_ == visible)
    return;

  visible_ = visible;

  if (visible)
    main_thread_missed_last_deadline_ = false;

  did_prepare_tiles_ = false;
}

void SchedulerStateMachine::SetBeginFrameSourcePaused(bool paused) {
  begin_frame_source_paused_ = paused;
}

void SchedulerStateMachine::SetResourcelessSoftwareDraw(
    bool resourceless_draw) {
  resourceless_draw_ = resourceless_draw;
}

void SchedulerStateMachine::SetCanDraw(bool can_draw) {
  can_draw_ = can_draw;
}

void SchedulerStateMachine::SetNeedsRedraw() {
  needs_redraw_ = true;
}

bool SchedulerStateMachine::OnlyImplSideUpdatesExpected() const {
  bool has_impl_updates = needs_redraw_ || needs_one_begin_impl_frame_;
  bool main_updates_expected =
      needs_begin_main_frame_ ||
      begin_main_frame_state_ != BEGIN_MAIN_FRAME_STATE_IDLE ||
      has_pending_tree_;
  return has_impl_updates && !main_updates_expected;
}

void SchedulerStateMachine::SetNeedsPrepareTiles() {
  if (!needs_prepare_tiles_) {
    TRACE_EVENT0("cc", "SchedulerStateMachine::SetNeedsPrepareTiles");
    needs_prepare_tiles_ = true;
  }
}
void SchedulerStateMachine::DidSubmitCompositorFrame() {
  TRACE_EVENT_ASYNC_BEGIN1("cc", "Scheduler:pending_submit_frames", this,
                           "pending_frames", pending_submit_frames_);
  DCHECK_LT(pending_submit_frames_, kMaxPendingSubmitFrames);

  pending_submit_frames_++;
  submit_frames_with_current_layer_tree_frame_sink_++;

  did_submit_in_last_frame_ = true;
  last_frame_number_submit_performed_ = current_frame_number_;
}

void SchedulerStateMachine::DidReceiveCompositorFrameAck() {
  TRACE_EVENT_ASYNC_END1("cc", "Scheduler:pending_submit_frames", this,
                         "pending_frames", pending_submit_frames_);
  pending_submit_frames_--;
}

void SchedulerStateMachine::SetTreePrioritiesAndScrollState(
    TreePriority tree_priority,
    ScrollHandlerState scroll_handler_state) {
  tree_priority_ = tree_priority;
  scroll_handler_state_ = scroll_handler_state;
}

void SchedulerStateMachine::SetCriticalBeginMainFrameToActivateIsFast(
    bool is_fast) {
  critical_begin_main_frame_to_activate_is_fast_ = is_fast;
}

bool SchedulerStateMachine::ImplLatencyTakesPriority() const {
  // Attempt to synchronize with the main thread if it has a scroll listener
  // and is fast.
  if (ScrollHandlerState::SCROLL_AFFECTS_SCROLL_HANDLER ==
          scroll_handler_state_ &&
      critical_begin_main_frame_to_activate_is_fast_)
    return false;

  // Don't wait for the main thread if we are prioritizing smoothness.
  if (SMOOTHNESS_TAKES_PRIORITY == tree_priority_)
    return true;

  return false;
}

void SchedulerStateMachine::SetNeedsBeginMainFrame() {
  needs_begin_main_frame_ = true;
}

void SchedulerStateMachine::SetNeedsOneBeginImplFrame() {
  needs_one_begin_impl_frame_ = true;
}

void SchedulerStateMachine::NotifyReadyToCommit() {
  DCHECK_EQ(begin_main_frame_state_, BEGIN_MAIN_FRAME_STATE_STARTED)
      << AsValue()->ToString();
  begin_main_frame_state_ = BEGIN_MAIN_FRAME_STATE_READY_TO_COMMIT;
  // In commit_to_active_tree mode, commit should happen right after BeginFrame,
  // meaning when this function is called, next action should be commit.
  if (settings_.commit_to_active_tree)
    DCHECK(ShouldCommit());
}

void SchedulerStateMachine::BeginMainFrameAborted(CommitEarlyOutReason reason) {
  DCHECK_EQ(begin_main_frame_state_, BEGIN_MAIN_FRAME_STATE_STARTED);

  // If the main thread aborted, it doesn't matter if the  main thread missed
  // the last deadline since it didn't have an update anyway.
  main_thread_missed_last_deadline_ = false;

  switch (reason) {
    case CommitEarlyOutReason::ABORTED_LAYER_TREE_FRAME_SINK_LOST:
    case CommitEarlyOutReason::ABORTED_NOT_VISIBLE:
    case CommitEarlyOutReason::ABORTED_DEFERRED_COMMIT:
      begin_main_frame_state_ = BEGIN_MAIN_FRAME_STATE_IDLE;
      SetNeedsBeginMainFrame();
      return;
    case CommitEarlyOutReason::FINISHED_NO_UPDATES:
      bool commit_has_no_updates = true;
      WillCommit(commit_has_no_updates);
      return;
  }
}

void SchedulerStateMachine::DidPrepareTiles() {
  needs_prepare_tiles_ = false;
  did_prepare_tiles_ = true;
}

void SchedulerStateMachine::DidLoseLayerTreeFrameSink() {
  if (layer_tree_frame_sink_state_ == LAYER_TREE_FRAME_SINK_NONE ||
      layer_tree_frame_sink_state_ == LAYER_TREE_FRAME_SINK_CREATING)
    return;
  layer_tree_frame_sink_state_ = LAYER_TREE_FRAME_SINK_NONE;
  needs_redraw_ = false;
}

bool SchedulerStateMachine::NotifyReadyToActivate() {
  if (!has_pending_tree_ || pending_tree_is_ready_for_activation_)
    return false;

  pending_tree_is_ready_for_activation_ = true;
  return true;
}

void SchedulerStateMachine::NotifyReadyToDraw() {
  active_tree_is_ready_to_draw_ = true;
}

void SchedulerStateMachine::DidCreateAndInitializeLayerTreeFrameSink() {
  DCHECK_EQ(layer_tree_frame_sink_state_, LAYER_TREE_FRAME_SINK_CREATING);
  layer_tree_frame_sink_state_ = LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_COMMIT;

  if (did_create_and_initialize_first_layer_tree_frame_sink_) {
    // TODO(boliu): See if we can remove this when impl-side painting is always
    // on. Does anything on the main thread need to update after recreate?
    needs_begin_main_frame_ = true;
  }
  did_create_and_initialize_first_layer_tree_frame_sink_ = true;
  pending_submit_frames_ = 0;
  submit_frames_with_current_layer_tree_frame_sink_ = 0;
  main_thread_missed_last_deadline_ = false;
}

void SchedulerStateMachine::NotifyBeginMainFrameStarted() {
  DCHECK_EQ(begin_main_frame_state_, BEGIN_MAIN_FRAME_STATE_SENT);
  begin_main_frame_state_ = BEGIN_MAIN_FRAME_STATE_STARTED;
}

bool SchedulerStateMachine::HasInitializedLayerTreeFrameSink() const {
  switch (layer_tree_frame_sink_state_) {
    case LAYER_TREE_FRAME_SINK_NONE:
    case LAYER_TREE_FRAME_SINK_CREATING:
      return false;

    case LAYER_TREE_FRAME_SINK_ACTIVE:
    case LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_COMMIT:
    case LAYER_TREE_FRAME_SINK_WAITING_FOR_FIRST_ACTIVATION:
      return true;
  }
  NOTREACHED();
  return false;
}

}  // namespace cc
