/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import { PureComponent } from "devtools/client/shared/vendor/react";
import PropTypes from "devtools/client/shared/vendor/react-prop-types";
import {
  toEditorPosition,
  fromEditorLine,
  getDocument,
  hasDocument,
  startOperation,
  endOperation,
  getTokenEnd,
} from "../../utils/editor/index";
import { isException } from "../../utils/pause/index";
import { getIndentation } from "../../utils/indentation";
import { connect } from "devtools/client/shared/vendor/react-redux";
import {
  getVisibleSelectedFrame,
  getPauseReason,
  getSourceTextContent,
  getCurrentThread,
} from "../../selectors/index";
import { isWasm } from "../../utils/wasm";
import { features } from "../../utils/prefs";

export class DebugLine extends PureComponent {
  debugExpression;

  static get propTypes() {
    return {
      editor: PropTypes.object,
      selectedSource: PropTypes.object,
      location: PropTypes.object,
      why: PropTypes.object,
    };
  }

  componentDidMount() {
    const { why, location } = this.props;
    if (features.codemirrorNext) {
      return;
    }
    this.setDebugLine(why, location);
  }

  componentWillUnmount() {
    const { why, location } = this.props;
    if (features.codemirrorNext) {
      return;
    }
    this.clearDebugLine(why, location);
  }

  componentDidUpdate(prevProps) {
    const { why, location, editor, selectedSource } = this.props;

    if (features.codemirrorNext) {
      if (!selectedSource) {
        return;
      }

      if (
        prevProps.location == this.props.location &&
        prevProps.selectedSource?.id == selectedSource?.id
      ) {
        return;
      }

      const { lineClass } = this.getTextClasses(why);
      // Remove the debug line marker when no longer paused, or the selected source
      // is no longer the source where the pause occured.
      if (!location || location.source.id !== selectedSource.id) {
        editor.removeLineContentMarker("debug-line-marker");
      } else {
        const isSourceWasm = isWasm(selectedSource.id);
        editor.setLineContentMarker({
          id: "debug-line-marker",
          lineClassName: lineClass,
          condition(line) {
            const lineNumber = fromEditorLine(
              selectedSource.id,
              line,
              isSourceWasm
            );
            const editorLocation = toEditorPosition(location);
            return editorLocation.line == lineNumber;
          },
        });
      }
    } else {
      startOperation();
      this.clearDebugLine(prevProps.why, prevProps.location);
      this.setDebugLine(why, location);
      endOperation();
    }
  }

  setDebugLine(why, location) {
    if (!location) {
      return;
    }
    const doc = getDocument(location.source.id);

    let { line, column } = toEditorPosition(location);
    let { markTextClass, lineClass } = this.getTextClasses(why);
    doc.addLineClass(line, "wrap", lineClass);

    const lineText = doc.getLine(line);
    column = Math.max(column, getIndentation(lineText));

    // If component updates because user clicks on
    // another source tab, codeMirror will be null.
    const columnEnd = doc.cm ? getTokenEnd(doc.cm, line, column) : null;

    if (columnEnd === null) {
      markTextClass += " to-line-end";
    }

    this.debugExpression = doc.markText(
      { ch: column, line },
      { ch: columnEnd, line },
      { className: markTextClass }
    );
  }

  clearDebugLine(why, location) {
    // Avoid clearing the line if we didn't set a debug line before,
    // or, if the document is no longer available
    if (!location || !hasDocument(location.source.id)) {
      return;
    }

    if (this.debugExpression) {
      this.debugExpression.clear();
    }

    const { line } = toEditorPosition(location);
    const doc = getDocument(location.source.id);
    const { lineClass } = this.getTextClasses(why);
    doc.removeLineClass(line, "wrap", lineClass);
  }

  getTextClasses(why) {
    if (why && isException(why)) {
      return {
        markTextClass: "debug-expression-error",
        lineClass: "new-debug-line-error",
      };
    }

    return { markTextClass: "debug-expression", lineClass: "new-debug-line" };
  }

  render() {
    return null;
  }
}

function isDocumentReady(location, sourceTextContent) {
  return location && sourceTextContent && hasDocument(location.source.id);
}

const mapStateToProps = state => {
  // Avoid unecessary intermediate updates when there is no location
  // or the source text content isn't yet fully loaded
  const frame = getVisibleSelectedFrame(state);
  const location = frame?.location;
  if (!location) {
    return {};
  }
  const sourceTextContent = getSourceTextContent(state, location);
  if (
    !features.codemirrorNext &&
    !isDocumentReady(location, sourceTextContent)
  ) {
    return {};
  }
  return {
    location,
    why: getPauseReason(state, getCurrentThread(state)),
  };
};

export default connect(mapStateToProps)(DebugLine);
