/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are Copyright (C) 1998
 * Netscape Communications Corporation.  All Rights Reserved.
 */
#include "nsFrameReflowState.h"
#include "nsIStyleContext.h"
#include "nsStyleConsts.h"
#include "nsIFrame.h"
#include "nsIHTMLReflow.h"
#include "nsIContent.h"
#include "nsHTMLAtoms.h"
#include "nsIPresContext.h"
#include "nsIPresShell.h"

PRBool
nsHTMLReflowState::HaveFixedContentWidth() const
{
  const nsStylePosition* pos;
  frame->GetStyleData(eStyleStruct_Position, (const nsStyleStruct*&)pos);
  // What about 'inherit'?
  return eStyleUnit_Auto != pos->mWidth.GetUnit();
}

PRBool
nsHTMLReflowState::HaveFixedContentHeight() const
{
  const nsStylePosition* pos;
  frame->GetStyleData(eStyleStruct_Position, (const nsStyleStruct*&)pos);
  // What about 'inherit'?
  return eStyleUnit_Auto != pos->mHeight.GetUnit();
}

const nsHTMLReflowState*
nsHTMLReflowState::GetContainingBlockReflowState(const nsReflowState* aParentRS)
{
  while (nsnull != aParentRS) {
    if (nsnull != aParentRS->frame) {
      PRBool isContainingBlock;
      // XXX This needs to go and we need to start using the info in the
      // reflow state...
      nsresult rv = aParentRS->frame->IsPercentageBase(isContainingBlock);
      if (NS_SUCCEEDED(rv) && isContainingBlock) {
        return (const nsHTMLReflowState*) aParentRS;
      }
    }
    aParentRS = aParentRS->parentReflowState;
  }
  return nsnull;
}

const nsHTMLReflowState*
nsHTMLReflowState::GetPageBoxReflowState(const nsReflowState* aParentRS)
{
  // XXX write me as soon as we can ask a frame if it's a page frame...
  return nsnull;
}

nscoord
nsHTMLReflowState::GetContainingBlockContentWidth(const nsReflowState* aParentRS)
{
  nscoord width = 0;
  const nsHTMLReflowState* rs =
    GetContainingBlockReflowState(aParentRS);
  if (nsnull != rs) {
    return ((nsHTMLReflowState*)aParentRS)->computedWidth;/* XXX cast */
  }
  return width;
}

static inline PRBool
IsReplaced(nsIAtom* aTag)
{
  return (nsHTMLAtoms::img == aTag) ||
    (nsHTMLAtoms::applet == aTag) ||
    (nsHTMLAtoms::object == aTag) ||
    (nsHTMLAtoms::input == aTag) ||
    (nsHTMLAtoms::select == aTag) ||
    (nsHTMLAtoms::textarea == aTag) ||
    (nsHTMLAtoms::iframe == aTag);
}

// XXX there is no CLEAN way to detect the "replaced" attribute (yet)
void
nsHTMLReflowState::DetermineFrameType(nsIPresContext& aPresContext)
{
  nsIAtom* tag = nsnull;
  nsIContent* content;
  if ((NS_OK == frame->GetContent(content)) && (nsnull != content)) {
    content->GetTag(tag);
    NS_RELEASE(content);
  }

  // Section 9.7 indicates that absolute position takes precedence
  // over float which takes precedence over display.
  const nsStyleDisplay* display;
  frame->GetStyleData(eStyleStruct_Display, (const nsStyleStruct*&)display);
  const nsStylePosition* pos;
  frame->GetStyleData(eStyleStruct_Position, (const nsStyleStruct*&)pos);
  if ((nsnull != pos) && (NS_STYLE_POSITION_ABSOLUTE == pos->mPosition)) {
    frameType = NS_CSS_FRAME_TYPE_ABSOLUTE;
  }
  else if (NS_STYLE_FLOAT_NONE != display->mFloats) {
    frameType = NS_CSS_FRAME_TYPE_FLOATING;
  }
  else {
    switch (display->mDisplay) {
    case NS_STYLE_DISPLAY_BLOCK:
    case NS_STYLE_DISPLAY_LIST_ITEM:
    case NS_STYLE_DISPLAY_TABLE:
      frameType = NS_CSS_FRAME_TYPE_BLOCK;
      break;

    case NS_STYLE_DISPLAY_INLINE:
    case NS_STYLE_DISPLAY_MARKER:
    case NS_STYLE_DISPLAY_INLINE_TABLE:
      frameType = NS_CSS_FRAME_TYPE_INLINE;
      break;

    case NS_STYLE_DISPLAY_RUN_IN:
    case NS_STYLE_DISPLAY_COMPACT:
      // XXX need to look ahead at the frame's sibling
      frameType = NS_CSS_FRAME_TYPE_BLOCK;
      break;

    case NS_STYLE_DISPLAY_TABLE_CELL:
    case NS_STYLE_DISPLAY_TABLE_CAPTION:
    case NS_STYLE_DISPLAY_TABLE_ROW_GROUP:
    case NS_STYLE_DISPLAY_TABLE_COLUMN:
    case NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP:
    case NS_STYLE_DISPLAY_TABLE_HEADER_GROUP:
    case NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP:
    case NS_STYLE_DISPLAY_TABLE_ROW:
      frameType = NS_CSS_FRAME_TYPE_INTERNAL_TABLE;
      break;

    case NS_STYLE_DISPLAY_NONE:
    default:
      frameType = NS_CSS_FRAME_TYPE_UNKNOWN;
      break;
    }
  }

  if (IsReplaced(tag)) {
    frameType = NS_FRAME_REPLACED(frameType);
  }
  NS_IF_RELEASE(tag);
}

// Helper function that re-calculates the left and right margin based on
// the width of the containing block, the border/padding, and the computed
// width.
//
// This function is called by InitConstraints() when the 'width' property
// has a value other than 'auto'
void
nsHTMLReflowState::CalculateLeftRightMargin(const nsHTMLReflowState* aContainingBlockRS,
                                            const nsStyleSpacing*    aSpacing,
                                            nscoord                  aComputedWidth,
                                            const nsMargin&          aBorderPadding,
                                            nscoord&                 aComputedLeftMargin,
                                            nscoord&                 aComputedRightMargin)
{
  PRBool  isAutoLeftMargin = eStyleUnit_Auto == aSpacing->mMargin.GetLeftUnit();
  PRBool  isAutoRightMargin = eStyleUnit_Auto == aSpacing->mMargin.GetRightUnit();
  
  // Calculate how much space is available for margins
  nscoord availMarginSpace = aContainingBlockRS->computedWidth - aComputedWidth -
                             aBorderPadding.left - aBorderPadding.right;

  // See whether we're over constrained
  if (!isAutoLeftMargin && !isAutoRightMargin) {
    // Neither margin is 'auto' so we're over constrained. Use the
    // 'direction' property to tell which margin to ignore
    const nsStyleDisplay* display;
    frame->GetStyleData(eStyleStruct_Display, (const nsStyleStruct*&)display);

    if (NS_STYLE_DIRECTION_LTR == display->mDirection) {
      isAutoRightMargin = PR_TRUE;
    } else {
      isAutoLeftMargin = PR_TRUE;
    }
  }

  if (isAutoLeftMargin) {
    if (isAutoRightMargin) {
      // Both margins are 'auto' so their computed values are equal
      if (availMarginSpace <= 0) {
        aComputedLeftMargin = aComputedRightMargin = 0;
      } else {
        aComputedLeftMargin = availMarginSpace / 2;
        aComputedRightMargin = availMarginSpace - aComputedLeftMargin;
      }
    } else {
      aComputedLeftMargin = PR_MAX(0, availMarginSpace - aComputedRightMargin);
    }

  } else if (isAutoRightMargin) {
    aComputedRightMargin = PR_MAX(0, availMarginSpace - aComputedLeftMargin);
  }
}

void
nsHTMLReflowState::ComputeRelativeOffsets(const nsHTMLReflowState* cbrs,
                                          const nsStylePosition* aPosition)
{
  nsStyleCoord              coord;
  const nsHTMLReflowState*  pcbrs = nsnull;

  // If any of the offsets are 'inherit' we need to find the positioned
  // containing block 
  if ((eStyleUnit_Inherit == aPosition->mOffset.GetLeftUnit()) ||
      (eStyleUnit_Inherit == aPosition->mOffset.GetTopUnit()) ||
      (eStyleUnit_Inherit == aPosition->mOffset.GetRightUnit()) ||
      (eStyleUnit_Inherit == aPosition->mOffset.GetBottomUnit())) {

    pcbrs = cbrs;

    while (nsnull != pcbrs) {
      const nsStylePosition* position;
      pcbrs->frame->GetStyleData(eStyleStruct_Position, (const nsStyleStruct*&)position);

      if ((NS_STYLE_POSITION_ABSOLUTE == position->mPosition) ||
          (NS_STYLE_POSITION_RELATIVE == position->mPosition)) {
        break;
      }

      pcbrs = (const nsHTMLReflowState*)pcbrs->parentReflowState;  // XXX cast
    }
  }

  // For relatively positioned elements 'auto' becomes 0
  if (eStyleUnit_Inherit == aPosition->mOffset.GetLeftUnit()) {
    computedOffsets.left = pcbrs ? pcbrs->computedOffsets.left : 0;
  } else if (eStyleUnit_Auto == aPosition->mOffset.GetLeftUnit()) {
    computedOffsets.left = 0;
  } else {
    ComputeHorizontalValue(cbrs->computedWidth, aPosition->mOffset.GetLeftUnit(),
                           aPosition->mOffset.GetLeft(coord),
                           computedOffsets.left);
  }

  if (eStyleUnit_Inherit == aPosition->mOffset.GetTopUnit()) {
    computedOffsets.top = pcbrs ? pcbrs->computedOffsets.top : 0;
  } else if ((eStyleUnit_Auto == aPosition->mOffset.GetTopUnit()) ||
      ((NS_AUTOHEIGHT == cbrs->computedHeight) &&
       (eStyleUnit_Percent == aPosition->mOffset.GetTopUnit()))) {
    computedOffsets.top = 0;
  } else {
    ComputeVerticalValue(cbrs->computedHeight, aPosition->mOffset.GetTopUnit(),
                         aPosition->mOffset.GetTop(coord),
                         computedOffsets.top);
  }

  if (eStyleUnit_Inherit == aPosition->mOffset.GetRightUnit()) {
    computedOffsets.right = pcbrs ? pcbrs->computedOffsets.right : 0;
  } else if (eStyleUnit_Auto == aPosition->mOffset.GetRightUnit()) {
    computedOffsets.right = 0;
  } else {
    ComputeHorizontalValue(cbrs->computedWidth, aPosition->mOffset.GetRightUnit(),
                           aPosition->mOffset.GetRight(coord),
                           computedOffsets.right);
  }

  if (eStyleUnit_Inherit == aPosition->mOffset.GetBottomUnit()) {
    computedOffsets.bottom = pcbrs ? pcbrs->computedOffsets.bottom : 0;
  } else if ((eStyleUnit_Auto == aPosition->mOffset.GetBottomUnit()) ||
      ((NS_AUTOHEIGHT == cbrs->computedHeight) &&
      (eStyleUnit_Percent == aPosition->mOffset.GetBottomUnit()))) {
    computedOffsets.bottom = 0;
  } else {
    ComputeVerticalValue(cbrs->computedHeight, aPosition->mOffset.GetBottomUnit(),
                         aPosition->mOffset.GetBottom(coord),
                         computedOffsets.bottom);
  }
}

void
nsHTMLReflowState::InitAbsoluteConstraints(nsIPresContext& aPresContext,
                                           const nsHTMLReflowState* cbrs,
                                           const nsStylePosition* aPosition,
                                           nscoord containingBlockWidth,
                                           nscoord containingBlockHeight)
{
  const nsStyleSpacing* spacing;
  frame->GetStyleData(eStyleStruct_Spacing, (const nsStyleStruct*&)spacing);
  const nsStyleDisplay* display;
  frame->GetStyleData(eStyleStruct_Display, (const nsStyleStruct*&)display);

  // If any of the offsets are 'auto', then get the placeholder frame and compute
  // its origin relative to the containing block
  nsPoint placeholderOffset(0, 0);
  if ((eStyleUnit_Auto == aPosition->mOffset.GetLeftUnit()) ||
      (eStyleUnit_Auto == aPosition->mOffset.GetTopUnit()) ||
      (eStyleUnit_Auto == aPosition->mOffset.GetRightUnit()) ||
      (eStyleUnit_Auto == aPosition->mOffset.GetBottomUnit())) {
    
    // Get the placeholder frame
    nsIFrame*     placeholderFrame;
    nsIPresShell* presShell = aPresContext.GetShell();

    presShell->GetPlaceholderFrameFor(frame, placeholderFrame);
    NS_RELEASE(presShell);
    NS_ASSERTION(nsnull != placeholderFrame, "no placeholder frame");
    if (nsnull != placeholderFrame) {
      placeholderFrame->GetOrigin(placeholderOffset);

      nsIFrame* parent;
      placeholderFrame->GetParent(parent);
      while ((nsnull != parent) && (parent != cbrs->frame)) {
        nsPoint origin;

        parent->GetOrigin(origin);
        placeholderOffset += origin;
        parent->GetParent(parent);
      }
    }
  }

  // Compute border and padding
  nsMargin borderPadding;
  ComputeBorderPaddingFor(frame, parentReflowState, borderPadding);

  nsStyleUnit widthUnit = aPosition->mWidth.GetUnit();
  nsStyleUnit heightUnit = aPosition->mHeight.GetUnit();

  // Initialize the 'left' and 'right' computed offsets
  PRBool        leftIsAuto = PR_FALSE, rightIsAuto = PR_FALSE;
  nsStyleCoord  coord;
  if (eStyleUnit_Inherit == aPosition->mOffset.GetLeftUnit()) {
    computedOffsets.left = cbrs->computedOffsets.left;
  } else if (eStyleUnit_Auto == aPosition->mOffset.GetLeftUnit()) {
    if (NS_STYLE_DIRECTION_LTR == display->mDirection) {
      computedOffsets.left = placeholderOffset.x;
    } else {
      computedOffsets.left = 0;
      leftIsAuto = PR_TRUE;
    }
  } else {
    ComputeHorizontalValue(containingBlockWidth, aPosition->mOffset.GetLeftUnit(),
                           aPosition->mOffset.GetLeft(coord),
                           computedOffsets.left);
  }
  if (eStyleUnit_Inherit == aPosition->mOffset.GetRightUnit()) {
    computedOffsets.right = cbrs->computedOffsets.right;
  } else if (eStyleUnit_Auto == aPosition->mOffset.GetRightUnit()) {
    if (NS_STYLE_DIRECTION_RTL == display->mDirection) {
      computedOffsets.right = placeholderOffset.x;
    } else {
      computedOffsets.right = 0;
      rightIsAuto = PR_TRUE;
    }
  } else {
    ComputeHorizontalValue(containingBlockWidth, aPosition->mOffset.GetRightUnit(),
                           aPosition->mOffset.GetRight(coord),
                           computedOffsets.right);
  }

  // Calculate the computed width
  if (eStyleUnit_Auto == widthUnit) {
    // Any remaining 'auto' values for 'left', 'right', 'margin-left', or
    // 'margin-right' are replaced with 0 (their default value)
    computedWidth = containingBlockWidth - computedOffsets.left -
      computedMargin.left - borderPadding.left - borderPadding.right -
      computedMargin.right - computedOffsets.right;

  } else {
    // Use the specified value for the computed width
    ComputeHorizontalValue(containingBlockWidth, widthUnit, aPosition->mWidth,
                           computedWidth);

    if (leftIsAuto) {
      // Any 'auto' on 'margin-left' or 'margin-right' are replaced with 0
      // (their default value)
      computedOffsets.left = containingBlockWidth - computedMargin.left -
        borderPadding.left - computedWidth - borderPadding.right -
        computedMargin.right - computedOffsets.right;

    } else if (rightIsAuto) {
      // Any 'auto' on 'margin-left' or 'margin-right' are replaced with 0
      // (their default value)
      computedOffsets.right = containingBlockWidth - computedOffsets.left -
        computedMargin.left - borderPadding.left - computedWidth -
        borderPadding.right - computedMargin.right;

    } else {
      // All that's left to solve for are 'auto' values for 'margin-left' and
      // 'margin-right'
      if ((eStyleUnit_Auto == spacing->mMargin.GetLeftUnit()) ||
          (eStyleUnit_Auto == spacing->mMargin.GetRightUnit())) {

        nscoord availMarginSpace = containingBlockWidth - computedOffsets.left -
          borderPadding.left - computedWidth - borderPadding.right -
          computedOffsets.right;

        if (availMarginSpace > 0) {
          if (eStyleUnit_Auto == spacing->mMargin.GetLeftUnit()) {
            if (eStyleUnit_Auto == spacing->mMargin.GetRightUnit()) {
              // Both 'margin-left' and 'margin-right' are 'auto', so they get
              // equal values
              computedMargin.left = availMarginSpace / 2;
              computedMargin.right = availMarginSpace - computedMargin.left;
            } else {
              computedMargin.left = availMarginSpace;
            }
          } else {
            computedMargin.right = availMarginSpace;
          }
        }
      }
    }
  }

  // Initialize the 'top' and 'bottom' computed offsets
  PRBool  bottomIsAuto = PR_FALSE;
  if (eStyleUnit_Inherit == aPosition->mOffset.GetTopUnit()) {
    computedOffsets.top = cbrs->computedOffsets.top;
  } else if ((eStyleUnit_Auto == aPosition->mOffset.GetTopUnit()) ||
      ((NS_AUTOHEIGHT == containingBlockHeight) &&
       (eStyleUnit_Percent == aPosition->mOffset.GetTopUnit()))) {
    // Use the placeholder position
    computedOffsets.top = placeholderOffset.y;
  } else {
    nsStyleCoord  coord;
    ComputeVerticalValue(containingBlockHeight, aPosition->mOffset.GetTopUnit(),
                         aPosition->mOffset.GetTop(coord), computedOffsets.top);
  }
  if (eStyleUnit_Inherit == aPosition->mOffset.GetBottomUnit()) {
    computedOffsets.bottom = cbrs->computedOffsets.bottom;
  } else if ((eStyleUnit_Auto == aPosition->mOffset.GetBottomUnit()) ||
      ((NS_AUTOHEIGHT == containingBlockHeight) &&
       (eStyleUnit_Percent == aPosition->mOffset.GetBottomUnit()))) {
    if (eStyleUnit_Auto == heightUnit) {
      computedOffsets.bottom = 0;        
    } else {
      bottomIsAuto = PR_TRUE;
    }
  } else {
    nsStyleCoord  coord;
    ComputeVerticalValue(containingBlockHeight, aPosition->mOffset.GetBottomUnit(),
                         aPosition->mOffset.GetBottom(coord), computedOffsets.bottom);
  }

  // Check for a percentage based height and a containing block height
  // that depends on its content height, i.e., not explicitly specified
  if (eStyleUnit_Percent == heightUnit) {
    if (NS_AUTOHEIGHT == containingBlockHeight) {
      // Interpret the height like 'auto'
      heightUnit = eStyleUnit_Auto;
    }
  }

  // Calculate the computed height
  if (eStyleUnit_Auto == heightUnit) {
    // Any 'auto' on 'margin-top' or 'margin-bottom' are replaced with 0
    // (their default value)
    if (NS_FRAME_IS_REPLACED(frameType)) {
      computedHeight = NS_INTRINSICSIZE;
    } else {
      // If the containing block's height was not explicitly specified (i.e.,
      // it depends on its content height), then so does our height
      if (NS_AUTOHEIGHT == containingBlockHeight) {
        computedHeight = NS_AUTOHEIGHT;

      } else {
        computedHeight = containingBlockHeight - computedOffsets.top - 
          computedMargin.top - borderPadding.top - borderPadding.bottom -
          computedMargin.bottom - computedOffsets.bottom;
      }
    }
  } else {
    // Use the specified value for the computed height
    ComputeVerticalValue(containingBlockHeight, heightUnit, aPosition->mHeight,
                         computedHeight);

    if (NS_AUTOHEIGHT != containingBlockHeight) {
      if (bottomIsAuto) {
        // Any 'auto' on 'margin-top' or 'margin-bottom' are replaced with 0
        computedOffsets.bottom = containingBlockHeight - computedOffsets.top -
          computedMargin.top - borderPadding.top - computedHeight -
          borderPadding.bottom - computedMargin.bottom;
  
      } else {
        // All that's left to solve for are 'auto' values for 'margin-top' and
        // 'margin-bottom'
        if ((eStyleUnit_Auto == spacing->mMargin.GetTopUnit()) ||
            (eStyleUnit_Auto == spacing->mMargin.GetBottomUnit())) {
  
          nscoord availMarginSpace = containingBlockHeight - computedOffsets.top -
            borderPadding.top - computedHeight - borderPadding.bottom -
            computedOffsets.bottom;
  
          if (availMarginSpace > 0) {
            if (eStyleUnit_Auto == spacing->mMargin.GetTopUnit()) {
              if (eStyleUnit_Auto == spacing->mMargin.GetBottomUnit()) {
                // Both 'margin-top' and 'margin-bottom' are 'auto', so they get
                // equal values
                computedMargin.top = availMarginSpace / 2;
                computedMargin.bottom = availMarginSpace - computedMargin.top;
              } else {
                computedMargin.top = availMarginSpace;
              }
            } else {
              computedMargin.bottom = availMarginSpace;
            }
          }
        }
      }
    }
  }
}

void
nsHTMLReflowState::InitConstraints(nsIPresContext& aPresContext)
{
  const nsStylePosition* pos;
  nsresult result = frame->GetStyleData(eStyleStruct_Position,
                                        (const nsStyleStruct*&)pos);

  // If this is the root frame then set the computed width and
  // height equal to the available space
  if (nsnull == parentReflowState) {
    computedWidth = availableWidth;
    computedHeight = availableHeight;
    computedMargin.SizeTo(0, 0, 0, 0);

  } else {
    // Get the containing block reflow state
    const nsHTMLReflowState* cbrs =
      GetContainingBlockReflowState(parentReflowState);
    NS_ASSERTION(nsnull != cbrs, "no containing block");

    // See if the element is relatively positioned
    if (NS_STYLE_POSITION_RELATIVE == pos->mPosition) {
      ComputeRelativeOffsets(cbrs, pos);
    } else {
      // Initialize offsets to 0
      computedOffsets.SizeTo(0, 0, 0, 0);
    }

    // Compute margins from the specified margin style information. These
    // become the default computed values, and may be adjusted below
    ComputeMarginFor(frame, parentReflowState, computedMargin);
  
    // Calculate the line height.
    // XXX Do we need to do this for all elements or just inline non-replaced
    // elements?
    mLineHeight = CalcLineHeight(aPresContext, frame);
  
    // See if it's an inline non-replaced element
    if (NS_CSS_FRAME_TYPE_INLINE == frameType) {
      // 'width' property doesn't apply to inline non-replaced elements. The
      // 'height' is given by the element's 'line-height' value
      if (mLineHeight >= 0) {
        computedHeight = mLineHeight;
      }
      return;  // nothing else to compute
    }

    // Get the containing block width and height. We'll need them when
    // calculating the computed width and height. For all elements other
    // than absolutely positioned elements, the containing block is formed
    // by the content edge
    nscoord containingBlockWidth = cbrs->computedWidth;
    nscoord containingBlockHeight = cbrs->computedHeight;
    //NS_ASSERTION(0 != containingBlockWidth, "containing block width of 0");
    nsStyleUnit widthUnit = pos->mWidth.GetUnit();
    nsStyleUnit heightUnit = pos->mHeight.GetUnit();

    // It's possible the child's max width is less than the width of the
    // containing block (e.g., for scrolled elements because we subtract for
    // the scrollbar width).
    // XXX Don't do this if the max width is 0, which is the case for floaters...
    if ((availableWidth < containingBlockWidth) && (availableWidth > 0)) {
      containingBlockWidth = availableWidth;
    }

    // Check for a percentage based width and an unconstrained containing
    // block width
    if (eStyleUnit_Percent == widthUnit) {
      if (NS_UNCONSTRAINEDSIZE == containingBlockWidth) {
        // Interpret the width like 'auto'
        widthUnit = eStyleUnit_Auto;
      }
    }
    // Check for a percentage based height and a containing block height
    // that depends on the content height
    if (eStyleUnit_Percent == heightUnit) {
      if (NS_AUTOHEIGHT == containingBlockHeight) {
        // Interpret the height like 'auto'
        heightUnit = eStyleUnit_Auto;
      }
    }

    // Calculate the computed width and height. This varies by frame type
    if ((NS_FRAME_REPLACED(NS_CSS_FRAME_TYPE_INLINE) == frameType) ||
        (NS_FRAME_REPLACED(NS_CSS_FRAME_TYPE_FLOATING) == frameType)) {
      // Inline replaced element and floating replaced element are basically
      // treated the same
      if (eStyleUnit_Auto == widthUnit) {
        // A specified value of 'auto' uses the element's intrinsic width
        computedWidth = NS_INTRINSICSIZE;
      } else {
        ComputeHorizontalValue(containingBlockWidth, widthUnit, pos->mWidth,
                               computedWidth);
      }
      if (eStyleUnit_Auto == heightUnit) {
        // A specified value of 'auto' uses the element's intrinsic height
        computedHeight = NS_INTRINSICSIZE;
      } else {
        ComputeVerticalValue(containingBlockHeight, heightUnit, pos->mHeight,
                             computedHeight);
      }

    } else if (NS_CSS_FRAME_TYPE_FLOATING == frameType) {
      // Floating non-replaced element
      if (eStyleUnit_Auto == widthUnit) {
        // A specified value of 'auto' becomes a computed width of 0
        computedWidth = 0;
      } else {
        ComputeHorizontalValue(containingBlockWidth, widthUnit, pos->mWidth,
                               computedWidth);
      }
      if (eStyleUnit_Auto == heightUnit) {
        computedHeight = NS_AUTOHEIGHT;  // let it choose its height
      } else {
        ComputeVerticalValue(containingBlockHeight, heightUnit, pos->mHeight,
                             computedHeight);
      }

    } else if (NS_CSS_FRAME_TYPE_INTERNAL_TABLE == frameType) {
      // Internal table elements. The rules vary depending on the type
      const nsStyleDisplay* display;
      frame->GetStyleData(eStyleStruct_Display, (const nsStyleStruct*&)display);

      // Calculate the computed width
      if ((NS_STYLE_DISPLAY_TABLE_ROW == display->mDisplay) ||
          (NS_STYLE_DISPLAY_TABLE_ROW_GROUP == display->mDisplay)) {
        // 'width' property doesn't apply to table rows and row groups
        widthUnit = eStyleUnit_Auto;
      }

      if (eStyleUnit_Auto == widthUnit) {
        computedWidth = availableWidth;

        if (computedWidth != NS_UNCONSTRAINEDSIZE) {
          // Internal table elements don't have margins, but they have border
          // and padding
          nsMargin borderPadding;
          ComputeBorderPaddingFor(frame, parentReflowState, borderPadding);
          computedWidth -= borderPadding.left + borderPadding.right;
        }
      
      } else {
        ComputeHorizontalValue(containingBlockWidth, widthUnit, pos->mWidth,
                               computedWidth);
      }

      // Calculate the computed height
      if ((NS_STYLE_DISPLAY_TABLE_COLUMN == display->mDisplay) ||
          (NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP == display->mDisplay)) {
        // 'height' property doesn't apply to table columns and column groups
        heightUnit = eStyleUnit_Auto;
      }
      if (eStyleUnit_Auto == heightUnit) {
        computedHeight = NS_AUTOHEIGHT;
      } else {
        ComputeVerticalValue(containingBlockHeight, heightUnit, pos->mHeight,
                             computedHeight);
      }

    } else if (NS_FRAME_GET_TYPE(frameType) == NS_CSS_FRAME_TYPE_ABSOLUTE) {
      InitAbsoluteConstraints(aPresContext, cbrs, pos, containingBlockWidth,
                              containingBlockHeight);
    
    } else {
      // Block-level elements
      const nsStyleSpacing* spacing;
      frame->GetStyleData(eStyleStruct_Spacing, (const nsStyleStruct*&)spacing);

      // Compute border and padding
      nsMargin borderPadding;
      ComputeBorderPaddingFor(frame, parentReflowState, borderPadding);
  
      // Compute the content width
      if (eStyleUnit_Auto == widthUnit) {
        if (NS_FRAME_IS_REPLACED(frameType)) {
          // Block-level replaced element in the flow. A specified value of 
          // 'auto' uses the element's intrinsic width
          computedWidth = NS_INTRINSICSIZE;

        } else {
          // Block-level non-replaced element in the flow
          if (NS_UNCONSTRAINEDSIZE == containingBlockWidth) {
            computedWidth = NS_UNCONSTRAINEDSIZE;
          } else {
            // Block-level non-replaced element in the flow. 'auto' values for
            // margin-left and margin-right become 0 and the sum of the areas must
            // equal the width of the containing block
            computedWidth = containingBlockWidth - computedMargin.left -
              computedMargin.right - borderPadding.left - borderPadding.right;
          }
        }
        
      } else {
        ComputeHorizontalValue(containingBlockWidth, widthUnit, pos->mWidth,
                               computedWidth);

        // Calculate the computed left and right margin again taking into
        // account the computed width, border/padding, and width of the
        // containing block
        CalculateLeftRightMargin(cbrs, spacing, computedWidth, borderPadding,
                                 computedMargin.left, computedMargin.right);
      }
  
      // Compute the content height
      if (eStyleUnit_Auto == heightUnit) {
        if (NS_FRAME_IS_REPLACED(frameType)) {
          computedHeight = NS_INTRINSICSIZE;
        } else {
          computedHeight = NS_AUTOHEIGHT;
        }
      } else {
        ComputeVerticalValue(containingBlockHeight, heightUnit, pos->mHeight,
                             computedHeight);
      }
    }
  }
}

nscoord
nsHTMLReflowState::CalcLineHeight(nsIPresContext& aPresContext,
                                  nsIFrame* aFrame)
{
  nscoord lineHeight = -1;
  nsIStyleContext* sc;
  aFrame->GetStyleContext(sc);
  const nsStyleFont* elementFont = nsnull;
  if (nsnull != sc) {
    elementFont = (const nsStyleFont*)sc->GetStyleData(eStyleStruct_Font);
    for (;;) {
      const nsStyleText* text = (const nsStyleText*)
        sc->GetStyleData(eStyleStruct_Text);
      if (nsnull != text) {
        nsStyleUnit unit = text->mLineHeight.GetUnit();
#ifdef NOISY_VERTICAL_ALIGN
        printf("  styleUnit=%d\n", unit);
#endif
        if (eStyleUnit_Enumerated == unit) {
          // Normal value; we use 1.0 for normal
          // XXX could come from somewhere else
          break;
        } else if (eStyleUnit_Factor == unit) {
          if (nsnull != elementFont) {
            // CSS2 spec says that the number is inherited, not the
            // computed value. Therefore use the font size of the
            // element times the inherited number.
            nscoord size = elementFont->mFont.size;
            lineHeight = nscoord(size * text->mLineHeight.GetFactorValue());
          }
          break;
        }
        else if (eStyleUnit_Coord == unit) {
          lineHeight = text->mLineHeight.GetCoordValue();
          break;
        }
        else if (eStyleUnit_Percent == unit) {
          // XXX This could arguably be the font-metrics actual height
          // instead since the spec says use the computed height.
          const nsStyleFont* font = (const nsStyleFont*)
            sc->GetStyleData(eStyleStruct_Font);
          nscoord size = font->mFont.size;
          lineHeight = nscoord(size * text->mLineHeight.GetPercentValue());
          break;
        }
        else if (eStyleUnit_Inherit == unit) {
          nsIStyleContext* parentSC;
          parentSC = sc->GetParent();
          if (nsnull == parentSC) {
            // Note: Break before releasing to avoid double-releasing sc
            break;
          }
          NS_RELEASE(sc);
          sc = parentSC;
        }
        else {
          // other units are not part of the spec so don't bother
          // looping
          break;
        }
      }
    }
    NS_RELEASE(sc);
  }

  return lineHeight;
}

void
nsHTMLReflowState::ComputeHorizontalValue(nscoord aContainingBlockWidth,
                                          nsStyleUnit aUnit,
                                          const nsStyleCoord& aCoord,
                                          nscoord& aResult)
{
  NS_PRECONDITION(eStyleUnit_Inherit != aUnit, "unexpected unit");
  aResult = 0;
  if (eStyleUnit_Percent == aUnit) {
    float pct = aCoord.GetPercentValue();
    aResult = NSToCoordFloor(aContainingBlockWidth * pct);
  
  } else if (eStyleUnit_Coord == aUnit) {
    aResult = aCoord.GetCoordValue();
  }
}

void
nsHTMLReflowState::ComputeVerticalValue(nscoord aContainingBlockHeight,
                                        nsStyleUnit aUnit,
                                        const nsStyleCoord& aCoord,
                                        nscoord& aResult)
{
  NS_PRECONDITION(eStyleUnit_Inherit != aUnit, "unexpected unit");
  aResult = 0;
  if (eStyleUnit_Percent == aUnit) {
    // Verify no one is trying to calculate a percentage based height against
    // a height that's shrink wrapping to its content. In that case they should
    // treat the specified value like 'auto'
    NS_ASSERTION(NS_AUTOHEIGHT != aContainingBlockHeight, "unexpected containing block height");
    float pct = aCoord.GetPercentValue();
    aResult = NSToCoordFloor(aContainingBlockHeight * pct);

  } else if (eStyleUnit_Coord == aUnit) {
    aResult = aCoord.GetCoordValue();
  }
}

void
nsHTMLReflowState::ComputeMarginFor(nsIFrame* aFrame,
                                    const nsReflowState* aParentRS,
                                    nsMargin& aResult)
{
  const nsStyleSpacing* spacing;
  nsresult rv;
  rv = aFrame->GetStyleData(eStyleStruct_Spacing,
                            (const nsStyleStruct*&) spacing);
  if (NS_SUCCEEDED(rv) && (nsnull != spacing)) {
    // If style style can provide us the margin directly, then use it.
#if 0
    if (!spacing->GetMargin(aResult) && (nsnull != aParentRS))
#else
    spacing->CalcMarginFor(aFrame, aResult);
    if ((eStyleUnit_Percent == spacing->mMargin.GetTopUnit()) ||
        (eStyleUnit_Percent == spacing->mMargin.GetRightUnit()) ||
        (eStyleUnit_Percent == spacing->mMargin.GetBottomUnit()) ||
        (eStyleUnit_Percent == spacing->mMargin.GetLeftUnit()))
#endif
    {
      // We have to compute the value (because it's uncomputable by
      // the style code).
      const nsHTMLReflowState* rs = GetContainingBlockReflowState(aParentRS);
      if (nsnull != rs) {
        nsStyleCoord left, right;
        nscoord containingBlockWidth = rs->computedWidth;
        ComputeHorizontalValue(containingBlockWidth, spacing->mMargin.GetLeftUnit(),
                               spacing->mMargin.GetLeft(left), aResult.left);
        ComputeHorizontalValue(containingBlockWidth, spacing->mMargin.GetRightUnit(),
                               spacing->mMargin.GetRight(right),
                               aResult.right);
      }
      else {
        aResult.left = 0;
        aResult.right = 0;
      }

      const nsHTMLReflowState* rs2 = GetPageBoxReflowState(aParentRS);
      nsStyleCoord top, bottom;
      if (nsnull != rs2) {
        // According to the CSS2 spec, margin percentages are
        // calculated with respect to the *height* of the containing
        // block when in a paginated context.
        ComputeVerticalValue(rs2->computedHeight, spacing->mMargin.GetTopUnit(),
                             spacing->mMargin.GetTop(top), aResult.top);
        ComputeVerticalValue(rs2->computedHeight, spacing->mMargin.GetBottomUnit(),
                             spacing->mMargin.GetBottom(bottom),
                             aResult.bottom);
      }
      else if (nsnull != rs) {
        // According to the CSS2 spec, margin percentages are
        // calculated with respect to the *width* of the containing
        // block, even for margin-top and margin-bottom.
        nscoord containingBlockWidth = rs->computedWidth;
        ComputeHorizontalValue(containingBlockWidth, spacing->mMargin.GetTopUnit(),
                               spacing->mMargin.GetTop(top), aResult.top);
        ComputeHorizontalValue(containingBlockWidth, spacing->mMargin.GetBottomUnit(),
                               spacing->mMargin.GetBottom(bottom),
                               aResult.bottom);
      }
      else {
        aResult.top = 0;
        aResult.bottom = 0;
      }
    }
  }
}

void
nsHTMLReflowState::ComputePaddingFor(nsIFrame* aFrame,
                                     const nsReflowState* aParentRS,
                                     nsMargin& aResult)
{
  const nsStyleSpacing* spacing;
  nsresult rv;
  rv = aFrame->GetStyleData(eStyleStruct_Spacing,
                            (const nsStyleStruct*&) spacing);
  if (NS_SUCCEEDED(rv) && (nsnull != spacing)) {
    // If style can provide us the padding directly, then use it.
    spacing->CalcPaddingFor(aFrame, aResult);
#if 0
    if (!spacing->GetPadding(aResult) && (nsnull != aParentRS))
#else
    spacing->CalcPaddingFor(aFrame, aResult);
    if ((eStyleUnit_Percent == spacing->mPadding.GetTopUnit()) ||
        (eStyleUnit_Percent == spacing->mPadding.GetRightUnit()) ||
        (eStyleUnit_Percent == spacing->mPadding.GetBottomUnit()) ||
        (eStyleUnit_Percent == spacing->mPadding.GetLeftUnit()))
#endif
    {
      // We have to compute the value (because it's uncomputable by
      // the style code).
      const nsHTMLReflowState* rs = GetContainingBlockReflowState(aParentRS);
      if (nsnull != rs) {
        nsStyleCoord left, right, top, bottom;
        nscoord containingBlockWidth = rs->computedWidth;
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetLeftUnit(),
                               spacing->mPadding.GetLeft(left), aResult.left);
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetRightUnit(),
                               spacing->mPadding.GetRight(right),
                               aResult.right);

        // According to the CSS2 spec, padding percentages are
        // calculated with respect to the *width* of the containing
        // block, even for padding-top and padding-bottom.
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetTopUnit(),
                               spacing->mPadding.GetTop(top), aResult.top);
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetBottomUnit(),
                               spacing->mPadding.GetBottom(bottom),
                               aResult.bottom);
      }
      else {
        aResult.SizeTo(0, 0, 0, 0);
      }
    }
  }
  else {
    aResult.SizeTo(0, 0, 0, 0);
  }
}

void
nsHTMLReflowState::ComputeBorderFor(nsIFrame* aFrame,
                                    nsMargin& aResult)
{
  const nsStyleSpacing* spacing;
  nsresult rv;
  rv = aFrame->GetStyleData(eStyleStruct_Spacing,
                            (const nsStyleStruct*&) spacing);
  if (NS_SUCCEEDED(rv) && (nsnull != spacing)) {
    // If style can provide us the border directly, then use it.
    if (!spacing->GetBorder(aResult)) {
      // CSS2 has no percentage borders
      aResult.SizeTo(0, 0, 0, 0);
    }
  }
  else {
    aResult.SizeTo(0, 0, 0, 0);
  }
}

void
nsHTMLReflowState::ComputeBorderPaddingFor(nsIFrame* aFrame,
                                           const nsReflowState* aParentRS,
                                           nsMargin& aResult)
{
  const nsStyleSpacing* spacing;
  nsresult rv;
  rv = aFrame->GetStyleData(eStyleStruct_Spacing,
                            (const nsStyleStruct*&) spacing);
  if (NS_SUCCEEDED(rv) && (nsnull != spacing)) {
    nsMargin b, p;

    // If style style can provide us the margin directly, then use it.
    if (!spacing->GetBorder(b)) {
      b.SizeTo(0, 0, 0, 0);
    }
#if 0
    if (!spacing->GetPadding(p) && (nsnull != aParentRS))
#else
    spacing->CalcPaddingFor(aFrame, p);
    if ((eStyleUnit_Percent == spacing->mPadding.GetTopUnit()) ||
        (eStyleUnit_Percent == spacing->mPadding.GetRightUnit()) ||
        (eStyleUnit_Percent == spacing->mPadding.GetBottomUnit()) ||
        (eStyleUnit_Percent == spacing->mPadding.GetLeftUnit()))
#endif
    {
      // We have to compute the value (because it's uncomputable by
      // the style code).
      const nsHTMLReflowState* rs = GetContainingBlockReflowState(aParentRS);
      if (nsnull != rs) {
        nsStyleCoord left, right, top, bottom;
        nscoord containingBlockWidth = rs->computedWidth;
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetLeftUnit(),
                               spacing->mPadding.GetLeft(left), p.left);
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetRightUnit(),
                               spacing->mPadding.GetRight(right), p.right);

        // According to the CSS2 spec, padding percentages are
        // calculated with respect to the *width* of the containing
        // block, even for padding-top and padding-bottom.
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetTopUnit(),
                               spacing->mPadding.GetTop(top), p.top);
        ComputeHorizontalValue(containingBlockWidth, spacing->mPadding.GetBottomUnit(),
                               spacing->mPadding.GetBottom(bottom), p.bottom);
      }
      else {
        p.SizeTo(0, 0, 0, 0);
      }
    }

    aResult.top = b.top + p.top;
    aResult.right = b.right + p.right;
    aResult.bottom = b.bottom + p.bottom;
    aResult.left = b.left + p.left;
  }
  else {
    aResult.SizeTo(0, 0, 0, 0);
  }
}

//----------------------------------------------------------------------

nsFrameReflowState::nsFrameReflowState(nsIPresContext& aPresContext,
                                       const nsHTMLReflowState& aReflowState,
                                       const nsHTMLReflowMetrics& aMetrics)
  : nsHTMLReflowState(aReflowState),
    mPresContext(aPresContext)
{
  // While we skip around the reflow state that our parent gave us so
  // that the parentReflowState is linked properly, we don't want to
  // skip over it's reason.
  reason = aReflowState.reason;
  mNextRCFrame = nsnull;

  // Initialize max-element-size
  mComputeMaxElementSize = nsnull != aMetrics.maxElementSize;
  mMaxElementSize.width = 0;
  mMaxElementSize.height = 0;

  // Get style data that we need
  frame->GetStyleData(eStyleStruct_Text,
                      (const nsStyleStruct*&) mStyleText);
  frame->GetStyleData(eStyleStruct_Display,
                      (const nsStyleStruct*&) mStyleDisplay);
  frame->GetStyleData(eStyleStruct_Spacing,
                      (const nsStyleStruct*&) mStyleSpacing);

  // Calculate our border and padding value
  ComputeBorderPaddingFor(frame, parentReflowState, mBorderPadding);

  // Set mNoWrap flag
  switch (mStyleText->mWhiteSpace) {
  case NS_STYLE_WHITESPACE_PRE:
  case NS_STYLE_WHITESPACE_NOWRAP:
    mNoWrap = PR_TRUE;
    break;
  default:
    mNoWrap = PR_FALSE;
    break;
  }

  // Set mDirection value
  mDirection = mStyleDisplay->mDirection;

  // See if this container frame will act as a root for margin
  // collapsing behavior.
  mIsMarginRoot = PR_FALSE;
  if ((0 != mBorderPadding.top) || (0 != mBorderPadding.bottom)) {
    mIsMarginRoot = PR_TRUE;
  }
  mCarriedOutTopMargin = 0;
  mPrevBottomMargin = 0;
  mCarriedOutMarginFlags = 0;
}

nsFrameReflowState::~nsFrameReflowState()
{
}

void
nsFrameReflowState::SetupChildReflowState(nsHTMLReflowState& aChildRS)
{
  // Get reflow reason set correctly. It's possible that a child was
  // created and then it was decided that it could not be reflowed
  // (for example, a block frame that isn't at the start of a
  // line). In this case the reason will be wrong so we need to check
  // the frame state.
  nsReflowReason reason = eReflowReason_Resize;
  nsIFrame* frame = aChildRS.frame;
  nsFrameState state;
  frame->GetFrameState(state);
  if (NS_FRAME_FIRST_REFLOW & state) {
    reason = eReflowReason_Initial;
  }
  else if (mNextRCFrame == frame) {
    reason = eReflowReason_Incremental;
    // Make sure we only incrementally reflow once
    mNextRCFrame = nsnull;/* XXX bad coupling */
  }
  aChildRS.reason = reason;
}

