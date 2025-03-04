/*
 * Copyright (C) 2005 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/commands/DeleteSelectionCommand.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/Text.h"
#include "core/editing/EditingBoundary.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/RelocatablePosition.h"
#include "core/editing/SelectionTemplate.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/VisibleUnits.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLBRElement.h"
#include "core/html/HTMLStyleElement.h"
#include "core/html/HTMLTableRowElement.h"
#include "core/html/forms/HTMLInputElement.h"
#include "core/html_names.h"
#include "core/layout/LayoutTableCell.h"

namespace blink {

using namespace HTMLNames;

static bool IsTableCellEmpty(Node* cell) {
  DCHECK(cell);
  DCHECK(IsTableCell(cell)) << cell;
  return VisiblePosition::FirstPositionInNode(*cell).DeepEquivalent() ==
         VisiblePosition::LastPositionInNode(*cell).DeepEquivalent();
}

static bool IsTableRowEmpty(Node* row) {
  if (!IsHTMLTableRowElement(row))
    return false;

  row->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  for (Node* child = row->firstChild(); child; child = child->nextSibling()) {
    if (IsTableCell(child) && !IsTableCellEmpty(child))
      return false;
  }
  return true;
}

static bool CanMergeListElements(Element* first_list, Element* second_list) {
  if (!first_list || !second_list || first_list == second_list)
    return false;

  return CanMergeLists(*first_list, *second_list);
}

DeleteSelectionCommand::DeleteSelectionCommand(
    Document& document,
    bool smart_delete,
    bool merge_blocks_after_delete,
    bool expand_for_special_elements,
    bool sanitize_markup,
    InputEvent::InputType input_type,
    const Position& reference_move_position)
    : CompositeEditCommand(document),
      has_selection_to_delete_(false),
      smart_delete_(smart_delete),
      merge_blocks_after_delete_(merge_blocks_after_delete),
      need_placeholder_(false),
      expand_for_special_elements_(expand_for_special_elements),
      prune_start_block_if_necessary_(false),
      starts_at_empty_line_(false),
      sanitize_markup_(sanitize_markup),
      input_type_(input_type),
      reference_move_position_(reference_move_position),
      start_block_(nullptr),
      end_block_(nullptr),
      typing_style_(nullptr),
      delete_into_blockquote_style_(nullptr) {}

DeleteSelectionCommand::DeleteSelectionCommand(
    const VisibleSelection& selection,
    bool smart_delete,
    bool merge_blocks_after_delete,
    bool expand_for_special_elements,
    bool sanitize_markup,
    InputEvent::InputType input_type)
    : CompositeEditCommand(*selection.Start().GetDocument()),
      has_selection_to_delete_(true),
      smart_delete_(smart_delete),
      merge_blocks_after_delete_(merge_blocks_after_delete),
      need_placeholder_(false),
      expand_for_special_elements_(expand_for_special_elements),
      prune_start_block_if_necessary_(false),
      starts_at_empty_line_(false),
      sanitize_markup_(sanitize_markup),
      input_type_(input_type),
      selection_to_delete_(selection),
      start_block_(nullptr),
      end_block_(nullptr),
      typing_style_(nullptr),
      delete_into_blockquote_style_(nullptr) {}

void DeleteSelectionCommand::InitializeStartEnd(Position& start,
                                                Position& end) {
  DCHECK(!GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      GetDocument().Lifecycle());

  HTMLElement* start_special_container = nullptr;
  HTMLElement* end_special_container = nullptr;

  start = selection_to_delete_.Start();
  end = selection_to_delete_.End();

  // For HRs, we'll get a position at (HR,1) when hitting delete from the
  // beginning of the previous line, or (HR,0) when forward deleting, but in
  // these cases, we want to delete it, so manually expand the selection
  if (IsHTMLHRElement(*start.AnchorNode()))
    start = Position::BeforeNode(*start.AnchorNode());
  else if (IsHTMLHRElement(*end.AnchorNode()))
    end = Position::AfterNode(*end.AnchorNode());

  // FIXME: This is only used so that moveParagraphs can avoid the bugs in
  // special element expansion.
  if (!expand_for_special_elements_)
    return;

  while (1) {
    start_special_container = 0;
    end_special_container = 0;

    Position s =
        PositionBeforeContainingSpecialElement(start, &start_special_container);
    Position e =
        PositionAfterContainingSpecialElement(end, &end_special_container);

    if (!start_special_container && !end_special_container)
      break;

    if (CreateVisiblePosition(start).DeepEquivalent() !=
            selection_to_delete_.VisibleStart().DeepEquivalent() ||
        CreateVisiblePosition(end).DeepEquivalent() !=
            selection_to_delete_.VisibleEnd().DeepEquivalent())
      break;

    // If we're going to expand to include the startSpecialContainer, it must be
    // fully selected.
    if (start_special_container && !end_special_container &&
        ComparePositions(Position::InParentAfterNode(*start_special_container),
                         end) > -1)
      break;

    // If we're going to expand to include the endSpecialContainer, it must be
    // fully selected.
    if (end_special_container && !start_special_container &&
        ComparePositions(
            start, Position::InParentBeforeNode(*end_special_container)) > -1)
      break;

    if (start_special_container &&
        start_special_container->IsDescendantOf(end_special_container)) {
      // Don't adjust the end yet, it is the end of a special element that
      // contains the start special element (which may or may not be fully
      // selected).
      start = s;
    } else if (end_special_container &&
               end_special_container->IsDescendantOf(start_special_container)) {
      // Don't adjust the start yet, it is the start of a special element that
      // contains the end special element (which may or may not be fully
      // selected).
      end = e;
    } else {
      start = s;
      end = e;
    }
  }
}

void DeleteSelectionCommand::SetStartingSelectionOnSmartDelete(
    const Position& start,
    const Position& end) {
  DCHECK(!GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      GetDocument().Lifecycle());

  const bool is_base_first = StartingSelection().IsBaseFirst();
  // TODO(yosin): We should not call |createVisiblePosition()| here and use
  // |start| and |end| as base/extent since |VisibleSelection| also calls
  // |createVisiblePosition()| during construction.
  // Because of |newBase.affinity()| can be |Upstream|, we can't simply
  // use |start| and |end| here.
  VisiblePosition new_base = CreateVisiblePosition(is_base_first ? start : end);
  VisiblePosition new_extent =
      CreateVisiblePosition(is_base_first ? end : start);
  SelectionInDOMTree::Builder builder;
  builder.SetAffinity(new_base.Affinity())
      .SetBaseAndExtentDeprecated(new_base.DeepEquivalent(),
                                  new_extent.DeepEquivalent())
      .SetIsDirectional(StartingSelection().IsDirectional());
  const VisibleSelection& visible_selection =
      CreateVisibleSelection(builder.Build());
  SetStartingSelection(
      SelectionForUndoStep::From(visible_selection.AsSelection()));
}

void DeleteSelectionCommand::InitializePositionData(
    EditingState* editing_state) {
  DCHECK(!GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      GetDocument().Lifecycle());

  Position start, end;
  InitializeStartEnd(start, end);
  DCHECK(start.IsNotNull());
  DCHECK(end.IsNotNull());
  if (!IsEditablePosition(start)) {
    editing_state->Abort();
    return;
  }
  if (!IsEditablePosition(end)) {
    Node* highest_root = HighestEditableRoot(start);
    DCHECK(highest_root);
    end = LastEditablePositionBeforePositionInRoot(end, *highest_root);
  }

  upstream_start_ = MostBackwardCaretPosition(start);
  downstream_start_ = MostForwardCaretPosition(start);
  upstream_end_ = MostBackwardCaretPosition(end);
  downstream_end_ = MostForwardCaretPosition(end);

  start_root_ = RootEditableElementOf(start);
  end_root_ = RootEditableElementOf(end);

  start_table_row_ =
      ToHTMLTableRowElement(EnclosingNodeOfType(start, &IsHTMLTableRowElement));
  end_table_row_ =
      ToHTMLTableRowElement(EnclosingNodeOfType(end, &IsHTMLTableRowElement));

  // Don't move content out of a table cell.
  // If the cell is non-editable, enclosingNodeOfType won't return it by
  // default, so tell that function that we don't care if it returns
  // non-editable nodes.
  Node* start_cell = EnclosingNodeOfType(upstream_start_, &IsTableCell,
                                         kCanCrossEditingBoundary);
  Node* end_cell = EnclosingNodeOfType(downstream_end_, &IsTableCell,
                                       kCanCrossEditingBoundary);
  // FIXME: This isn't right.  A borderless table with two rows and a single
  // column would appear as two paragraphs.
  if (end_cell && end_cell != start_cell)
    merge_blocks_after_delete_ = false;

  // Usually the start and the end of the selection to delete are pulled
  // together as a result of the deletion. Sometimes they aren't (like when no
  // merge is requested), so we must choose one position to hold the caret
  // and receive the placeholder after deletion.
  VisiblePosition visible_end = CreateVisiblePosition(downstream_end_);
  if (merge_blocks_after_delete_ && !IsEndOfParagraph(visible_end))
    ending_position_ = downstream_end_;
  else
    ending_position_ = downstream_start_;

  // We don't want to merge into a block if it will mean changing the quote
  // level of content after deleting selections that contain a whole number
  // paragraphs plus a line break, since it is unclear to most users that such a
  // selection actually ends at the start of the next paragraph. This matches
  // TextEdit behavior for indented paragraphs.
  // Only apply this rule if the endingSelection is a range selection.  If it is
  // a caret, then other operations have created the selection we're deleting
  // (like the process of creating a selection to delete during a backspace),
  // and the user isn't in the situation described above.
  if (NumEnclosingMailBlockquotes(start) != NumEnclosingMailBlockquotes(end) &&
      IsStartOfParagraph(visible_end) &&
      IsStartOfParagraph(CreateVisiblePosition(start)) &&
      EndingSelection().IsRange()) {
    merge_blocks_after_delete_ = false;
    prune_start_block_if_necessary_ = true;
  }

  // Handle leading and trailing whitespace, as well as smart delete adjustments
  // to the selection
  leading_whitespace_ = LeadingWhitespacePosition(
      upstream_start_, selection_to_delete_.Affinity());
  trailing_whitespace_ = TrailingWhitespacePosition(downstream_end_);

  if (smart_delete_) {
    // skip smart delete if the selection to delete already starts or ends with
    // whitespace
    Position pos =
        CreateVisiblePosition(upstream_start_, selection_to_delete_.Affinity())
            .DeepEquivalent();
    bool skip_smart_delete =
        TrailingWhitespacePosition(pos, kConsiderNonCollapsibleWhitespace)
            .IsNotNull();
    if (!skip_smart_delete)
      skip_smart_delete =
          LeadingWhitespacePosition(downstream_end_, TextAffinity::kDefault,
                                    kConsiderNonCollapsibleWhitespace)
              .IsNotNull();

    // extend selection upstream if there is whitespace there
    bool has_leading_whitespace_before_adjustment =
        LeadingWhitespacePosition(upstream_start_,
                                  selection_to_delete_.Affinity(),
                                  kConsiderNonCollapsibleWhitespace)
            .IsNotNull();
    if (!skip_smart_delete && has_leading_whitespace_before_adjustment) {
      VisiblePosition visible_pos =
          PreviousPositionOf(CreateVisiblePosition(upstream_start_));
      pos = visible_pos.DeepEquivalent();
      // Expand out one character upstream for smart delete and recalculate
      // positions based on this change.
      upstream_start_ = MostBackwardCaretPosition(pos);
      downstream_start_ = MostForwardCaretPosition(pos);
      leading_whitespace_ =
          LeadingWhitespacePosition(upstream_start_, visible_pos.Affinity());

      SetStartingSelectionOnSmartDelete(upstream_start_, upstream_end_);
    }

    // trailing whitespace is only considered for smart delete if there is no
    // leading whitespace, as in the case where you double-click the first word
    // of a paragraph.
    if (!skip_smart_delete && !has_leading_whitespace_before_adjustment &&
        TrailingWhitespacePosition(downstream_end_,
                                   kConsiderNonCollapsibleWhitespace)
            .IsNotNull()) {
      // Expand out one character downstream for smart delete and recalculate
      // positions based on this change.
      pos = NextPositionOf(CreateVisiblePosition(downstream_end_))
                .DeepEquivalent();
      upstream_end_ = MostBackwardCaretPosition(pos);
      downstream_end_ = MostForwardCaretPosition(pos);
      trailing_whitespace_ = TrailingWhitespacePosition(downstream_end_);

      SetStartingSelectionOnSmartDelete(downstream_start_, downstream_end_);
    }
  }

  // We must pass call parentAnchoredEquivalent on the positions since some
  // editing positions that appear inside their nodes aren't really inside them.
  // [hr, 0] is one example.
  // FIXME: parentAnchoredEquivalent should eventually be moved into enclosing
  // element getters like the one below, since editing functions should
  // obviously accept editing positions.
  // FIXME: Passing false to enclosingNodeOfType tells it that it's OK to return
  // a non-editable node.  This was done to match existing behavior, but it
  // seems wrong.
  start_block_ =
      EnclosingNodeOfType(downstream_start_.ParentAnchoredEquivalent(),
                          &IsEnclosingBlock, kCanCrossEditingBoundary);
  end_block_ = EnclosingNodeOfType(upstream_end_.ParentAnchoredEquivalent(),
                                   &IsEnclosingBlock, kCanCrossEditingBoundary);
}

// We don't want to inherit style from an element which can't have contents.
static bool ShouldNotInheritStyleFrom(const Node& node) {
  return !node.CanContainRangeEndPoint();
}

void DeleteSelectionCommand::SaveTypingStyleState() {
  // A common case is deleting characters that are all from the same text node.
  // In that case, the style at the start of the selection before deletion will
  // be the same as the style at the start of the selection after deletion
  // (since those two positions will be identical). Therefore there is no need
  // to save the typing style at the start of the selection, nor is there a
  // reason to compute the style at the start of the selection after deletion
  // (see the early return in calculateTypingStyleAfterDelete).
  if (upstream_start_.AnchorNode() == downstream_end_.AnchorNode() &&
      upstream_start_.AnchorNode()->IsTextNode())
    return;

  if (ShouldNotInheritStyleFrom(*selection_to_delete_.Start().AnchorNode()))
    return;

  // Figure out the typing style in effect before the delete is done.
  typing_style_ = EditingStyle::Create(
      selection_to_delete_.Start(), EditingStyle::kEditingPropertiesInEffect);
  typing_style_->RemoveStyleAddedByElement(
      EnclosingAnchorElement(selection_to_delete_.Start()));

  // If we're deleting into a Mail blockquote, save the style at end() instead
  // of start(). We'll use this later in computeTypingStyleAfterDelete if we end
  // up outside of a Mail blockquote
  if (EnclosingNodeOfType(selection_to_delete_.Start(),
                          IsMailHTMLBlockquoteElement)) {
    delete_into_blockquote_style_ =
        EditingStyle::Create(selection_to_delete_.End());
    return;
  }
  delete_into_blockquote_style_ = nullptr;
}

bool DeleteSelectionCommand::HandleSpecialCaseBRDelete(
    EditingState* editing_state) {
  Node* node_after_upstream_start = upstream_start_.ComputeNodeAfterPosition();
  Node* node_after_downstream_start =
      downstream_start_.ComputeNodeAfterPosition();
  // Upstream end will appear before BR due to canonicalization
  Node* node_after_upstream_end = upstream_end_.ComputeNodeAfterPosition();

  if (!node_after_upstream_start || !node_after_downstream_start)
    return false;

  // Check for special-case where the selection contains only a BR on a line by
  // itself after another BR.
  bool upstream_start_is_br = IsHTMLBRElement(*node_after_upstream_start);
  bool downstream_start_is_br = IsHTMLBRElement(*node_after_downstream_start);
  bool is_br_on_line_by_itself =
      upstream_start_is_br && downstream_start_is_br &&
      node_after_downstream_start == node_after_upstream_end;
  if (is_br_on_line_by_itself) {
    RemoveNode(node_after_downstream_start, editing_state);
    return true;
  }

  // FIXME: This code doesn't belong in here.
  // We detect the case where the start is an empty line consisting of BR not
  // wrapped in a block element.
  if (upstream_start_is_br && downstream_start_is_br) {
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    if (!(IsStartOfBlock(
              VisiblePosition::BeforeNode(*node_after_upstream_start)) &&
          IsEndOfBlock(
              VisiblePosition::AfterNode(*node_after_upstream_start)))) {
      starts_at_empty_line_ = true;
      ending_position_ = downstream_end_;
    }
  }

  return false;
}

static Position FirstEditablePositionInNode(Node* node) {
  DCHECK(node);
  Node* next = node;
  while (next && !HasEditableStyle(*next))
    next = NodeTraversal::Next(*next, node);
  return next ? FirstPositionInOrBeforeNode(*next) : Position();
}

void DeleteSelectionCommand::RemoveNode(
    Node* node,
    EditingState* editing_state,
    ShouldAssumeContentIsAlwaysEditable
        should_assume_content_is_always_editable) {
  if (!node)
    return;

  if (start_root_ != end_root_ && !(node->IsDescendantOf(start_root_.Get()) &&
                                    node->IsDescendantOf(end_root_.Get()))) {
    // If a node is not in both the start and end editable roots, remove it only
    // if its inside an editable region.
    if (!HasEditableStyle(*node->parentNode())) {
      // Don't remove non-editable atomic nodes.
      if (!node->hasChildren())
        return;
      // Search this non-editable region for editable regions to empty.
      Node* child = node->firstChild();
      while (child) {
        Node* next_child = child->nextSibling();
        RemoveNode(child, editing_state,
                   should_assume_content_is_always_editable);
        if (editing_state->IsAborted())
          return;
        // Bail if nextChild is no longer node's child.
        if (next_child && next_child->parentNode() != node)
          return;
        child = next_child;
      }

      // Don't remove editable regions that are inside non-editable ones, just
      // clear them.
      return;
    }
  }

  if (IsTableStructureNode(node) || IsRootEditableElement(*node)) {
    // Do not remove an element of table structure; remove its contents.
    // Likewise for the root editable element.
    Node* child = node->firstChild();
    while (child) {
      Node* remove = child;
      child = child->nextSibling();
      RemoveNode(remove, editing_state,
                 should_assume_content_is_always_editable);
      if (editing_state->IsAborted())
        return;
    }

    // Make sure empty cell has some height, if a placeholder can be inserted.
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    LayoutObject* r = node->GetLayoutObject();
    if (r && r->IsTableCell() && ToLayoutTableCell(r)->ContentHeight() <= 0) {
      Position first_editable_position = FirstEditablePositionInNode(node);
      if (first_editable_position.IsNotNull())
        InsertBlockPlaceholder(first_editable_position, editing_state);
    }
    return;
  }

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (node == start_block_) {
    VisiblePosition previous = PreviousPositionOf(
        VisiblePosition::FirstPositionInNode(*start_block_.Get()));
    if (previous.IsNotNull() && !IsEndOfBlock(previous))
      need_placeholder_ = true;
  }
  if (node == end_block_) {
    VisiblePosition next =
        NextPositionOf(VisiblePosition::LastPositionInNode(*end_block_.Get()));
    if (next.IsNotNull() && !IsStartOfBlock(next))
      need_placeholder_ = true;
  }

  // FIXME: Update the endpoints of the range being deleted.
  ending_position_ = ComputePositionForNodeRemoval(ending_position_, *node);
  leading_whitespace_ =
      ComputePositionForNodeRemoval(leading_whitespace_, *node);
  trailing_whitespace_ =
      ComputePositionForNodeRemoval(trailing_whitespace_, *node);

  CompositeEditCommand::RemoveNode(node, editing_state,
                                   should_assume_content_is_always_editable);
}

static void UpdatePositionForTextRemoval(Text* node,
                                         int offset,
                                         int count,
                                         Position& position) {
  if (!position.IsOffsetInAnchor() || position.ComputeContainerNode() != node)
    return;

  if (position.OffsetInContainerNode() > offset + count)
    position = Position(position.ComputeContainerNode(),
                        position.OffsetInContainerNode() - count);
  else if (position.OffsetInContainerNode() > offset)
    position = Position(position.ComputeContainerNode(), offset);
}

void DeleteSelectionCommand::DeleteTextFromNode(Text* node,
                                                unsigned offset,
                                                unsigned count) {
  // FIXME: Update the endpoints of the range being deleted.
  UpdatePositionForTextRemoval(node, offset, count, ending_position_);
  UpdatePositionForTextRemoval(node, offset, count, leading_whitespace_);
  UpdatePositionForTextRemoval(node, offset, count, trailing_whitespace_);
  UpdatePositionForTextRemoval(node, offset, count, downstream_end_);

  CompositeEditCommand::DeleteTextFromNode(node, offset, count);
}

void DeleteSelectionCommand::
    MakeStylingElementsDirectChildrenOfEditableRootToPreventStyleLoss(
        EditingState* editing_state) {
  Range* range = CreateRange(selection_to_delete_.ToNormalizedEphemeralRange());
  Node* node = range->FirstNode();
  while (node && node != range->PastLastNode()) {
    Node* next_node = NodeTraversal::Next(*node);
    if (IsHTMLStyleElement(*node) || IsHTMLLinkElement(*node)) {
      next_node = NodeTraversal::NextSkippingChildren(*node);
      Element* element = RootEditableElement(*node);
      if (element) {
        RemoveNode(node, editing_state);
        if (editing_state->IsAborted())
          return;
        AppendNode(node, element, editing_state);
        if (editing_state->IsAborted())
          return;
      }
    }
    node = next_node;
  }
}

void DeleteSelectionCommand::HandleGeneralDelete(EditingState* editing_state) {
  if (upstream_start_.IsNull())
    return;

  int start_offset = upstream_start_.ComputeEditingOffset();
  Node* start_node = upstream_start_.AnchorNode();
  DCHECK(start_node);

  MakeStylingElementsDirectChildrenOfEditableRootToPreventStyleLoss(
      editing_state);
  if (editing_state->IsAborted())
    return;

  // Never remove the start block unless it's a table, in which case we won't
  // merge content in.
  if (start_node == start_block_.Get() && !start_offset &&
      CanHaveChildrenForEditing(start_node) &&
      !IsHTMLTableElement(*start_node)) {
    start_offset = 0;
    start_node = NodeTraversal::Next(*start_node);
    if (!start_node)
      return;
  }

  if (start_offset >= CaretMaxOffset(start_node) && start_node->IsTextNode()) {
    Text* text = ToText(start_node);
    if (text->length() > (unsigned)CaretMaxOffset(start_node))
      DeleteTextFromNode(text, CaretMaxOffset(start_node),
                         text->length() - CaretMaxOffset(start_node));
  }

  if (start_offset >= EditingStrategy::LastOffsetForEditing(start_node)) {
    start_node = NodeTraversal::NextSkippingChildren(*start_node);
    start_offset = 0;
  }

  // Done adjusting the start.  See if we're all done.
  if (!start_node)
    return;

  if (start_node == downstream_end_.AnchorNode()) {
    if (downstream_end_.ComputeEditingOffset() - start_offset > 0) {
      if (start_node->IsTextNode()) {
        // in a text node that needs to be trimmed
        Text* text = ToText(start_node);
        DeleteTextFromNode(
            text, start_offset,
            downstream_end_.ComputeOffsetInContainerNode() - start_offset);
      } else {
        RemoveChildrenInRange(start_node, start_offset,
                              downstream_end_.ComputeEditingOffset(),
                              editing_state);
        if (editing_state->IsAborted())
          return;
        ending_position_ = upstream_start_;
      }
    }

    // The selection to delete is all in one node.
    if (!start_node->GetLayoutObject() ||
        (!start_offset && downstream_end_.AtLastEditingPositionForNode())) {
      RemoveNode(start_node, editing_state);
      if (editing_state->IsAborted())
        return;
    }
  } else {
    bool start_node_was_descendant_of_end_node =
        upstream_start_.AnchorNode()->IsDescendantOf(
            downstream_end_.AnchorNode());
    // The selection to delete spans more than one node.
    Node* node(start_node);

    if (start_offset > 0) {
      if (start_node->IsTextNode()) {
        // in a text node that needs to be trimmed
        Text* text = ToText(node);
        DeleteTextFromNode(text, start_offset, text->length() - start_offset);
        node = NodeTraversal::Next(*node);
      } else {
        node = NodeTraversal::ChildAt(*start_node, start_offset);
      }
    } else if (start_node == upstream_end_.AnchorNode() &&
               start_node->IsTextNode()) {
      Text* text = ToText(upstream_end_.AnchorNode());
      DeleteTextFromNode(text, 0, upstream_end_.ComputeOffsetInContainerNode());
    }

    // handle deleting all nodes that are completely selected
    while (node && node != downstream_end_.AnchorNode()) {
      if (ComparePositions(FirstPositionInOrBeforeNode(*node),
                           downstream_end_) >= 0) {
        // NodeTraversal::nextSkippingChildren just blew past the end position,
        // so stop deleting
        node = nullptr;
      } else if (!downstream_end_.AnchorNode()->IsDescendantOf(node)) {
        Node* next_node = NodeTraversal::NextSkippingChildren(*node);
        // if we just removed a node from the end container, update end position
        // so the check above will work
        downstream_end_ = ComputePositionForNodeRemoval(downstream_end_, *node);
        RemoveNode(node, editing_state);
        if (editing_state->IsAborted())
          return;
        node = next_node;
      } else {
        Node& n = NodeTraversal::LastWithinOrSelf(*node);
        if (downstream_end_.AnchorNode() == n &&
            downstream_end_.ComputeEditingOffset() >= CaretMaxOffset(&n)) {
          RemoveNode(node, editing_state);
          if (editing_state->IsAborted())
            return;
          node = nullptr;
        } else {
          node = NodeTraversal::Next(*node);
        }
      }
    }

    if (downstream_end_.AnchorNode() != start_node &&
        !upstream_start_.AnchorNode()->IsDescendantOf(
            downstream_end_.AnchorNode()) &&
        downstream_end_.IsConnected() &&
        downstream_end_.ComputeEditingOffset() >=
            CaretMinOffset(downstream_end_.AnchorNode())) {
      if (downstream_end_.AtLastEditingPositionForNode() &&
          !CanHaveChildrenForEditing(downstream_end_.AnchorNode())) {
        // The node itself is fully selected, not just its contents.  Delete it.
        RemoveNode(downstream_end_.AnchorNode(), editing_state);
      } else {
        if (downstream_end_.AnchorNode()->IsTextNode()) {
          // in a text node that needs to be trimmed
          Text* text = ToText(downstream_end_.AnchorNode());
          if (downstream_end_.ComputeEditingOffset() > 0) {
            DeleteTextFromNode(text, 0, downstream_end_.ComputeEditingOffset());
          }
          // Remove children of m_downstreamEnd.anchorNode() that come after
          // m_upstreamStart. Don't try to remove children if m_upstreamStart
          // was inside m_downstreamEnd.anchorNode() and m_upstreamStart has
          // been removed from the document, because then we don't know how many
          // children to remove.
          // FIXME: Make m_upstreamStart a position we update as we remove
          // content, then we can always know which children to remove.
        } else if (!(start_node_was_descendant_of_end_node &&
                     !upstream_start_.IsConnected())) {
          int offset = 0;
          if (upstream_start_.AnchorNode()->IsDescendantOf(
                  downstream_end_.AnchorNode())) {
            Node* n = upstream_start_.AnchorNode();
            while (n && n->parentNode() != downstream_end_.AnchorNode())
              n = n->parentNode();
            if (n)
              offset = n->NodeIndex() + 1;
          }
          RemoveChildrenInRange(downstream_end_.AnchorNode(), offset,
                                downstream_end_.ComputeEditingOffset(),
                                editing_state);
          if (editing_state->IsAborted())
            return;
          downstream_end_ =
              Position::EditingPositionOf(downstream_end_.AnchorNode(), offset);
        }
      }
    }
  }
}

void DeleteSelectionCommand::FixupWhitespace() {
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (leading_whitespace_.IsNotNull() &&
      !IsRenderedCharacter(leading_whitespace_) &&
      leading_whitespace_.AnchorNode()->IsTextNode()) {
    Text* text_node = ToText(leading_whitespace_.AnchorNode());
    DCHECK(!text_node->GetLayoutObject() ||
           text_node->GetLayoutObject()->Style()->CollapseWhiteSpace())
        << text_node;
    ReplaceTextInNode(text_node,
                      leading_whitespace_.ComputeOffsetInContainerNode(), 1,
                      NonBreakingSpaceString());
  }
  if (trailing_whitespace_.IsNotNull() &&
      !IsRenderedCharacter(trailing_whitespace_) &&
      trailing_whitespace_.AnchorNode()->IsTextNode()) {
    Text* text_node = ToText(trailing_whitespace_.AnchorNode());
    DCHECK(!text_node->GetLayoutObject() ||
           text_node->GetLayoutObject()->Style()->CollapseWhiteSpace())
        << text_node;
    ReplaceTextInNode(text_node,
                      trailing_whitespace_.ComputeOffsetInContainerNode(), 1,
                      NonBreakingSpaceString());
  }
}

// If a selection starts in one block and ends in another, we have to merge to
// bring content before the start together with content after the end.
void DeleteSelectionCommand::MergeParagraphs(EditingState* editing_state) {
  if (!merge_blocks_after_delete_) {
    if (prune_start_block_if_necessary_) {
      // We aren't going to merge into the start block, so remove it if it's
      // empty.
      Prune(start_block_, editing_state);
      if (editing_state->IsAborted())
        return;
      // Removing the start block during a deletion is usually an indication
      // that we need a placeholder, but not in this case.
      need_placeholder_ = false;
    }
    return;
  }

  // It shouldn't have been asked to both try and merge content into the start
  // block and prune it.
  DCHECK(!prune_start_block_if_necessary_);

  // FIXME: Deletion should adjust selection endpoints as it removes nodes so
  // that we never get into this state (4099839).
  if (!downstream_end_.IsConnected() || !upstream_start_.IsConnected())
    return;

  // FIXME: The deletion algorithm shouldn't let this happen.
  if (ComparePositions(upstream_start_, downstream_end_) > 0)
    return;

  // There's nothing to merge.
  if (upstream_start_ == downstream_end_)
    return;

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  VisiblePosition start_of_paragraph_to_move =
      CreateVisiblePosition(downstream_end_);
  VisiblePosition merge_destination = CreateVisiblePosition(upstream_start_);

  // m_downstreamEnd's block has been emptied out by deletion.  There is no
  // content inside of it to move, so just remove it.
  Element* end_block = EnclosingBlock(downstream_end_.AnchorNode());
  if (!end_block ||
      !end_block->contains(
          start_of_paragraph_to_move.DeepEquivalent().AnchorNode()) ||
      !start_of_paragraph_to_move.DeepEquivalent().AnchorNode()) {
    RemoveNode(EnclosingBlock(downstream_end_.AnchorNode()), editing_state);
    return;
  }

  RelocatablePosition relocatable_start(
      start_of_paragraph_to_move.DeepEquivalent());

  // We need to merge into m_upstreamStart's block, but it's been emptied out
  // and collapsed by deletion.
  if (!merge_destination.DeepEquivalent().AnchorNode() ||
      (!merge_destination.DeepEquivalent().AnchorNode()->IsDescendantOf(
           EnclosingBlock(upstream_start_.ComputeContainerNode())) &&
       (!merge_destination.DeepEquivalent().AnchorNode()->hasChildren() ||
        !upstream_start_.ComputeContainerNode()->hasChildren())) ||
      (starts_at_empty_line_ &&
       merge_destination.DeepEquivalent() !=
           start_of_paragraph_to_move.DeepEquivalent())) {
    InsertNodeAt(HTMLBRElement::Create(GetDocument()), upstream_start_,
                 editing_state);
    if (editing_state->IsAborted())
      return;
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    merge_destination = CreateVisiblePosition(upstream_start_);
    start_of_paragraph_to_move =
        CreateVisiblePosition(relocatable_start.GetPosition());
  }

  if (merge_destination.DeepEquivalent() ==
      start_of_paragraph_to_move.DeepEquivalent())
    return;

  VisiblePosition end_of_paragraph_to_move =
      EndOfParagraph(start_of_paragraph_to_move, kCanSkipOverEditingBoundary);

  if (merge_destination.DeepEquivalent() ==
      end_of_paragraph_to_move.DeepEquivalent())
    return;

  // If the merge destination and source to be moved are both list items of
  // different lists, merge them into single list.
  Node* list_item_in_first_paragraph =
      EnclosingNodeOfType(upstream_start_, IsListItem);
  Node* list_item_in_second_paragraph =
      EnclosingNodeOfType(downstream_end_, IsListItem);
  if (list_item_in_first_paragraph && list_item_in_second_paragraph &&
      CanMergeListElements(list_item_in_first_paragraph->parentElement(),
                           list_item_in_second_paragraph->parentElement())) {
    MergeIdenticalElements(list_item_in_first_paragraph->parentElement(),
                           list_item_in_second_paragraph->parentElement(),
                           editing_state);
    if (editing_state->IsAborted())
      return;
    ending_position_ = merge_destination.DeepEquivalent();
    return;
  }

  // The rule for merging into an empty block is: only do so if its farther to
  // the right.
  // FIXME: Consider RTL.
  if (!starts_at_empty_line_ && IsStartOfParagraph(merge_destination) &&
      AbsoluteCaretBoundsOf(start_of_paragraph_to_move).X() >
          AbsoluteCaretBoundsOf(merge_destination).X()) {
    if (IsHTMLBRElement(
            *MostForwardCaretPosition(merge_destination.DeepEquivalent())
                 .AnchorNode())) {
      RemoveNodeAndPruneAncestors(
          MostForwardCaretPosition(merge_destination.DeepEquivalent())
              .AnchorNode(),
          editing_state);
      if (editing_state->IsAborted())
        return;
      ending_position_ = relocatable_start.GetPosition();
      return;
    }
  }

  // Block images, tables and horizontal rules cannot be made inline with
  // content at mergeDestination.  If there is any
  // (!isStartOfParagraph(mergeDestination)), don't merge, just move
  // the caret to just before the selection we deleted. See
  // https://bugs.webkit.org/show_bug.cgi?id=25439
  if (IsRenderedAsNonInlineTableImageOrHR(
          start_of_paragraph_to_move.DeepEquivalent().AnchorNode()) &&
      !IsStartOfParagraph(merge_destination)) {
    ending_position_ = upstream_start_;
    return;
  }

  // moveParagraphs will insert placeholders if it removes blocks that would
  // require their use, don't let block removals that it does cause the
  // insertion of *another* placeholder.
  bool need_placeholder = need_placeholder_;
  bool paragraph_to_merge_is_empty =
      start_of_paragraph_to_move.DeepEquivalent() ==
      end_of_paragraph_to_move.DeepEquivalent();
  MoveParagraph(
      start_of_paragraph_to_move, end_of_paragraph_to_move, merge_destination,
      editing_state, kDoNotPreserveSelection,
      paragraph_to_merge_is_empty ? kDoNotPreserveStyle : kPreserveStyle);
  if (editing_state->IsAborted())
    return;
  need_placeholder_ = need_placeholder;
  // The endingPosition was likely clobbered by the move, so recompute it
  // (moveParagraph selects the moved paragraph).
  ending_position_ = EndingVisibleSelection().Start();
}

void DeleteSelectionCommand::RemovePreviouslySelectedEmptyTableRows(
    EditingState* editing_state) {
  if (end_table_row_ && end_table_row_->isConnected() &&
      end_table_row_ != start_table_row_) {
    Node* row = end_table_row_->previousSibling();
    while (row && row != start_table_row_) {
      Node* previous_row = row->previousSibling();
      if (IsTableRowEmpty(row)) {
        // Use a raw removeNode, instead of DeleteSelectionCommand's,
        // because that won't remove rows, it only empties them in
        // preparation for this function.
        CompositeEditCommand::RemoveNode(row, editing_state);
        if (editing_state->IsAborted())
          return;
      }
      row = previous_row;
    }
  }

  // Remove empty rows after the start row.
  if (start_table_row_ && start_table_row_->isConnected() &&
      start_table_row_ != end_table_row_) {
    Node* row = start_table_row_->nextSibling();
    while (row && row != end_table_row_) {
      Node* next_row = row->nextSibling();
      if (IsTableRowEmpty(row)) {
        CompositeEditCommand::RemoveNode(row, editing_state);
        if (editing_state->IsAborted())
          return;
      }
      row = next_row;
    }
  }

  if (end_table_row_ && end_table_row_->isConnected() &&
      end_table_row_ != start_table_row_) {
    if (IsTableRowEmpty(end_table_row_.Get())) {
      // Don't remove m_endTableRow if it's where we're putting the ending
      // selection.
      if (!ending_position_.AnchorNode()->IsDescendantOf(
              end_table_row_.Get())) {
        // FIXME: We probably shouldn't remove m_endTableRow unless it's
        // fully selected, even if it is empty. We'll need to start
        // adjusting the selection endpoints during deletion to know
        // whether or not m_endTableRow was fully selected here.
        CompositeEditCommand::RemoveNode(end_table_row_.Get(), editing_state);
        if (editing_state->IsAborted())
          return;
      }
    }
  }
}

void DeleteSelectionCommand::CalculateTypingStyleAfterDelete() {
  // Clearing any previously set typing style and doing an early return.
  if (!typing_style_) {
    GetDocument().GetFrame()->GetEditor().ClearTypingStyle();
    return;
  }

  // Compute the difference between the style before the delete and the style
  // now after the delete has been done. Set this style on the frame, so other
  // editing commands being composed with this one will work, and also cache it
  // on the command, so the LocalFrame::appliedEditing can set it after the
  // whole composite command has completed.

  // If we deleted into a blockquote, but are now no longer in a blockquote, use
  // the alternate typing style
  if (delete_into_blockquote_style_ &&
      !EnclosingNodeOfType(ending_position_, IsMailHTMLBlockquoteElement,
                           kCanCrossEditingBoundary))
    typing_style_ = delete_into_blockquote_style_;
  delete_into_blockquote_style_ = nullptr;

  typing_style_->PrepareToApplyAt(ending_position_);
  if (typing_style_->IsEmpty())
    typing_style_ = nullptr;
  // This is where we've deleted all traces of a style but not a whole paragraph
  // (that's handled above). In this case if we start typing, the new characters
  // should have the same style as the just deleted ones, but, if we change the
  // selection, come back and start typing that style should be lost.  Also see
  // preserveTypingStyle() below.
  GetDocument().GetFrame()->GetEditor().SetTypingStyle(typing_style_);
}

void DeleteSelectionCommand::ClearTransientState() {
  selection_to_delete_ = VisibleSelection();
  upstream_start_ = Position();
  downstream_start_ = Position();
  upstream_end_ = Position();
  downstream_end_ = Position();
  ending_position_ = Position();
  leading_whitespace_ = Position();
  trailing_whitespace_ = Position();
  reference_move_position_ = Position();
}

// This method removes div elements with no attributes that have only one child
// or no children at all.
void DeleteSelectionCommand::RemoveRedundantBlocks(
    EditingState* editing_state) {
  Node* node = ending_position_.ComputeContainerNode();
  Element* root_element = RootEditableElement(*node);

  while (node != root_element) {
    ABORT_EDITING_COMMAND_IF(!node);
    if (IsRemovableBlock(node)) {
      if (node == ending_position_.AnchorNode())
        UpdatePositionForNodeRemovalPreservingChildren(ending_position_, *node);

      CompositeEditCommand::RemoveNodePreservingChildren(node, editing_state);
      if (editing_state->IsAborted())
        return;
      node = ending_position_.AnchorNode();
    } else {
      node = node->parentNode();
    }
  }
}

void DeleteSelectionCommand::DoApply(EditingState* editing_state) {
  // If selection has not been set to a custom selection when the command was
  // created, use the current ending selection.
  if (!has_selection_to_delete_)
    selection_to_delete_ = EndingVisibleSelection();

  if (!selection_to_delete_.IsValidFor(GetDocument()) ||
      !selection_to_delete_.IsRange() ||
      !selection_to_delete_.IsContentEditable()) {
    // editing/execCommand/delete-non-editable-range-crash.html reaches here.
    return;
  }

  RelocatablePosition relocatable_reference_position(reference_move_position_);

  // save this to later make the selection with
  TextAffinity affinity = selection_to_delete_.Affinity();

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  Position downstream_end =
      MostForwardCaretPosition(selection_to_delete_.End());
  bool root_will_stay_open_without_placeholder =
      downstream_end.ComputeContainerNode() ==
          RootEditableElement(*downstream_end.ComputeContainerNode()) ||
      (downstream_end.ComputeContainerNode()->IsTextNode() &&
       downstream_end.ComputeContainerNode()->parentNode() ==
           RootEditableElement(*downstream_end.ComputeContainerNode()));
  bool line_break_at_end_of_selection_to_delete =
      LineBreakExistsAtVisiblePosition(selection_to_delete_.VisibleEnd());
  need_placeholder_ = !root_will_stay_open_without_placeholder &&
                      IsStartOfParagraph(selection_to_delete_.VisibleStart(),
                                         kCanCrossEditingBoundary) &&
                      IsEndOfParagraph(selection_to_delete_.VisibleEnd(),
                                       kCanCrossEditingBoundary) &&
                      !line_break_at_end_of_selection_to_delete;
  if (need_placeholder_) {
    // Don't need a placeholder when deleting a selection that starts just
    // before a table and ends inside it (we do need placeholders to hold
    // open empty cells, but that's handled elsewhere).
    if (Element* table =
            TableElementJustAfter(selection_to_delete_.VisibleStart())) {
      if (selection_to_delete_.End().AnchorNode()->IsDescendantOf(table))
        need_placeholder_ = false;
    }
  }

  // set up our state
  InitializePositionData(editing_state);
  if (editing_state->IsAborted())
    return;

  bool line_break_before_start = LineBreakExistsAtVisiblePosition(
      PreviousPositionOf(CreateVisiblePosition(upstream_start_)));

  // Delete any text that may hinder our ability to fixup whitespace after the
  // delete
  DeleteInsignificantTextDownstream(trailing_whitespace_);

  SaveTypingStyleState();

  // deleting just a BR is handled specially, at least because we do not
  // want to replace it with a placeholder BR!
  bool br_result = HandleSpecialCaseBRDelete(editing_state);
  if (editing_state->IsAborted())
    return;
  if (br_result) {
    CalculateTypingStyleAfterDelete();
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    SelectionInDOMTree::Builder builder;
    builder.SetAffinity(affinity);
    builder.SetIsDirectional(EndingSelection().IsDirectional());
    if (ending_position_.IsNotNull())
      builder.Collapse(ending_position_);
    const VisibleSelection& visible_selection =
        CreateVisibleSelection(builder.Build());
    SetEndingSelection(
        SelectionForUndoStep::From(visible_selection.AsSelection()));
    ClearTransientState();
    RebalanceWhitespace();
    return;
  }

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  HandleGeneralDelete(editing_state);
  if (editing_state->IsAborted())
    return;

  FixupWhitespace();

  MergeParagraphs(editing_state);
  if (editing_state->IsAborted())
    return;

  RemovePreviouslySelectedEmptyTableRows(editing_state);
  if (editing_state->IsAborted())
    return;

  if (!need_placeholder_ && root_will_stay_open_without_placeholder) {
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    VisiblePosition visual_ending = CreateVisiblePosition(ending_position_);
    bool has_placeholder =
        LineBreakExistsAtVisiblePosition(visual_ending) &&
        NextPositionOf(visual_ending, kCannotCrossEditingBoundary).IsNull();
    need_placeholder_ = has_placeholder && line_break_before_start &&
                        !line_break_at_end_of_selection_to_delete;
  }

  HTMLBRElement* placeholder =
      need_placeholder_ ? HTMLBRElement::Create(GetDocument()) : nullptr;

  if (placeholder) {
    if (sanitize_markup_) {
      RemoveRedundantBlocks(editing_state);
      if (editing_state->IsAborted())
        return;
    }
    // handleGeneralDelete cause DOM mutation events so |m_endingPosition|
    // can be out of document.
    if (ending_position_.IsConnected()) {
      InsertNodeAt(placeholder, ending_position_, editing_state);
      if (editing_state->IsAborted())
        return;
    }
  }

  RebalanceWhitespaceAt(ending_position_);

  CalculateTypingStyleAfterDelete();

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  SelectionInDOMTree::Builder builder;
  builder.SetAffinity(affinity);
  builder.SetIsDirectional(EndingSelection().IsDirectional());
  if (ending_position_.IsNotNull())
    builder.Collapse(ending_position_);
  const VisibleSelection& visible_selection =
      CreateVisibleSelection(builder.Build());
  SetEndingSelection(
      SelectionForUndoStep::From(visible_selection.AsSelection()));

  if (relocatable_reference_position.GetPosition().IsNull()) {
    ClearTransientState();
    return;
  }

  // This deletion command is part of a move operation, we need to cleanup after
  // deletion.
  reference_move_position_ = relocatable_reference_position.GetPosition();
  // If the node for the destination has been removed as a result of the
  // deletion, set the destination to the ending point after the deletion.
  // Fixes: <rdar://problem/3910425> REGRESSION (Mail): Crash in
  // ReplaceSelectionCommand; selection is empty, leading to null deref
  if (!reference_move_position_.IsConnected())
    reference_move_position_ = EndingVisibleSelection().Start();

  // Move selection shouldn't left empty <li> block.
  CleanupAfterDeletion(editing_state,
                       CreateVisiblePosition(reference_move_position_));
  if (editing_state->IsAborted())
    return;

  ClearTransientState();
}

InputEvent::InputType DeleteSelectionCommand::GetInputType() const {
  // |DeleteSelectionCommand| could be used with Cut, Menu Bar deletion and
  // |TypingCommand|.
  // 1. Cut and Menu Bar deletion should rely on correct |m_inputType|.
  // 2. |TypingCommand| will supply the |inputType()|, so |m_inputType| could
  // default to |InputType::None|.
  return input_type_;
}

// Normally deletion doesn't preserve the typing style that was present before
// it.  For example, type a character, Bold, then delete the character and start
// typing.  The Bold typing style shouldn't stick around.  Deletion should
// preserve a typing style that *it* sets, however.
bool DeleteSelectionCommand::PreservesTypingStyle() const {
  return typing_style_;
}

DEFINE_TRACE(DeleteSelectionCommand) {
  visitor->Trace(selection_to_delete_);
  visitor->Trace(upstream_start_);
  visitor->Trace(downstream_start_);
  visitor->Trace(upstream_end_);
  visitor->Trace(downstream_end_);
  visitor->Trace(ending_position_);
  visitor->Trace(leading_whitespace_);
  visitor->Trace(trailing_whitespace_);
  visitor->Trace(reference_move_position_);
  visitor->Trace(start_block_);
  visitor->Trace(end_block_);
  visitor->Trace(typing_style_);
  visitor->Trace(delete_into_blockquote_style_);
  visitor->Trace(start_root_);
  visitor->Trace(end_root_);
  visitor->Trace(start_table_row_);
  visitor->Trace(end_table_row_);
  visitor->Trace(temporary_placeholder_);
  CompositeEditCommand::Trace(visitor);
}

}  // namespace blink
