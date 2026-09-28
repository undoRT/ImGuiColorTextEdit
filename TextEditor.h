#pragma once

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <regex>
#include "imgui.h"

class TextEditor
{
public:
	enum class PaletteIndex
	{
		Default,
		Keyword,
		Number,
		String,
		CharLiteral,
		Punctuation,
		Preprocessor,
		Identifier,
		KnownIdentifier,
		PreprocIdentifier,
		Comment,
		MultiLineComment,
		Background,
		Cursor,
		Selection,
		ErrorMarker,
		Breakpoint,
		LineNumber,
		CurrentLineFill,
		CurrentLineFillInactive,
		CurrentLineEdge,
		// Semantic slots (filled by a host-side language server / analyzer).
		// They are ordinary palette entries, so a host that never sets a
		// semantic token simply never reaches these colors.
		SemVariable,
		SemParameter,
		SemFunction,
		SemType,
		SemField,     // FUNCTION_BLOCK / PROGRAM instances
		SemEnumerator,
		SemConstant,
		Max
	};

	enum class SelectionMode
	{
		Normal,
		Word,
		Line
	};

	struct Breakpoint
	{
		int mLine;
		bool mEnabled;
		std::string mCondition;

		Breakpoint()
			: mLine(-1)
			, mEnabled(false)
		{}
	};

	// Represents a character coordinate from the user's point of view,
	// i. e. consider an uniform grid (assuming fixed-width font) on the
	// screen as it is rendered, and each cell has its own coordinate, starting from 0.
	// Tabs are counted as [1..mTabSize] count empty spaces, depending on
	// how many space is necessary to reach the next tab stop.
	// For example, coordinate (1, 5) represents the character 'B' in a line "\tABC", when mTabSize = 4,
	// because it is rendered as "    ABC" on the screen.
	struct Coordinates
	{
		int mLine, mColumn;
		Coordinates() : mLine(0), mColumn(0) {}
		Coordinates(int aLine, int aColumn) : mLine(aLine), mColumn(aColumn)
		{
			assert(aLine >= 0);
			assert(aColumn >= 0);
		}
		static Coordinates Invalid() { static Coordinates invalid(-1, -1); return invalid; }

		bool operator ==(const Coordinates& o) const
		{
			return
				mLine == o.mLine &&
				mColumn == o.mColumn;
		}

		bool operator !=(const Coordinates& o) const
		{
			return
				mLine != o.mLine ||
				mColumn != o.mColumn;
		}

		bool operator <(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn < o.mColumn;
		}

		bool operator >(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn > o.mColumn;
		}

		bool operator <=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn <= o.mColumn;
		}

		bool operator >=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn >= o.mColumn;
		}
	};

	struct Identifier
	{
		Coordinates mLocation;
		std::string mDeclaration;
	};

	typedef std::string String;
	typedef std::unordered_map<std::string, Identifier> Identifiers;
	typedef std::unordered_set<std::string> Keywords;
	typedef std::map<int, std::string> ErrorMarkers;
	typedef std::unordered_set<int> Breakpoints;
	typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;
	typedef uint8_t Char;

	struct Glyph
	{
		Char mChar;
		PaletteIndex mColorIndex = PaletteIndex::Default;
		bool mComment : 1;
		bool mMultiLineComment : 1;
		bool mPreprocessor : 1;

		Glyph(Char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex),
			mComment(false), mMultiLineComment(false), mPreprocessor(false) {}
	};

	typedef std::vector<Glyph> Line;
	typedef std::vector<Line> Lines;

	struct LanguageDefinition
	{
		typedef std::pair<std::string, PaletteIndex> TokenRegexString;
		typedef std::vector<TokenRegexString> TokenRegexStrings;
		typedef bool(*TokenizeCallback)(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end, PaletteIndex & paletteIndex);

		std::string mName;
		Keywords mKeywords;
		Identifiers mIdentifiers;
		Identifiers mPreprocIdentifiers;
		std::string mCommentStart, mCommentEnd, mSingleLineComment;
		char mPreprocChar;
		bool mAutoIndentation;

		TokenizeCallback mTokenize;

		TokenRegexStrings mTokenRegexStrings;

		bool mCaseSensitive;

		LanguageDefinition()
			: mPreprocChar('#'), mAutoIndentation(true), mTokenize(nullptr), mCaseSensitive(true)
		{
		}

		static const LanguageDefinition& CPlusPlus();
		static const LanguageDefinition& HLSL();
		static const LanguageDefinition& GLSL();
		static const LanguageDefinition& C();
		static const LanguageDefinition& SQL();
		static const LanguageDefinition& AngelScript();
		static const LanguageDefinition& Lua();
	};

	TextEditor();
	~TextEditor();

	void SetLanguageDefinition(const LanguageDefinition& aLanguageDef);
	const LanguageDefinition& GetLanguageDefinition() const { return mLanguageDefinition; }

	const Palette& GetPalette() const { return mPaletteBase; }
	void SetPalette(const Palette& aValue);

	void SetErrorMarkers(const ErrorMarkers& aMarkers) { mErrorMarkers = aMarkers; }
	void SetBreakpoints(const Breakpoints& aMarkers) { mBreakpoints = aMarkers; }

	// ==========================================================================
	// Semantic tokens
	//
	// A host that runs a real analyzer (e.g. a compiler front-end) can push
	// resolved symbol spans here. They are re-applied by ColorizeRange() after
	// the regex colorizer runs, so they survive every re-colorization instead
	// of being wiped by the next keystroke.
	//
	// Columns are glyph indices into the line, [aColStart, aColEnd).
	// Overlapping spans are applied in insertion order, last write wins.
	// ==========================================================================
	/// @brief Set semantic spans and repaint (see SetSemanticToken)
	void SetSemanticToken(int aLine, int aColStart, int aColEnd, PaletteIndex aColor);
	void ClearSemanticTokens();
	bool HasSemanticTokens() const { return !mSemanticTokens.empty(); }

	/// @brief Force a full re-colorization pass
	/// @details Needed after pushing semantic tokens from outside, since the
	/// incremental colorizer only touches lines that the user edited.
	void Recolorize() { Colorize(0, -1); }

	/// @brief Convert a screen position into a line/column in the text
	/// @details Lets a host find the glyph under the mouse, for instance to
	/// resolve the identifier a Ctrl+Click or a hover refers to.
	Coordinates ScreenToCoordinates(const ImVec2& aPosition) const { return ScreenPosToCoordinates(aPosition); }

	// ==========================================================================
	// Keys the host is handling itself
	//
	// A suggestion list that owns the arrow keys has to take them away from the
	// editor, or the same key press walks the list and the caret at once. The
	// editor is muted for a whole frame otherwise, which also swallows the
	// characters typed in that frame and stops it raising io.WantTextInput, so
	// the host has to guess whether it still has the keyboard. Blocking the
	// individual keys instead keeps typing, undo and the shortcuts working, and
	// leaves the editor fully live on every other frame.
	//
	// The block lasts until ClearBlockedKeys(). The host sets it before
	// Render() and clears it straight after, so a key is only ever spent once.
	// ==========================================================================
	/// @brief Take one key away from the editor for the frame
	void BlockKey(ImGuiKey aKey)
	{
		if (aKey >= ImGuiKey_NamedKey_BEGIN && aKey < ImGuiKey_NamedKey_END)
		{
			const int index = aKey - ImGuiKey_NamedKey_BEGIN;
			mBlockedKeys[index / 32] |= 1u << (index % 32);
		}
	}

	/// @brief Hand every key back to the editor
	void ClearBlockedKeys()
	{
		for (int word = 0; word < BlockedKeyWords; ++word)
			mBlockedKeys[word] = 0;
	}

	/// @brief Whether the host has taken this key
	bool IsKeyBlocked(ImGuiKey aKey) const
	{
		if (aKey < ImGuiKey_NamedKey_BEGIN || aKey >= ImGuiKey_NamedKey_END)
			return false;
		const int index = aKey - ImGuiKey_NamedKey_BEGIN;
		return (mBlockedKeys[index / 32] & (1u << (index % 32))) != 0;
	}

	/// @brief IsKeyPressed(), but false for a key the host is handling
	/// @details Used in place of ImGui::IsKeyPressed() throughout
	/// HandleKeyboardInputs(). Text entry is unaffected: characters arrive
	/// through io.InputQueueCharacters, not through these checks.
	bool KeyPressed(ImGuiKey aKey) const { return !IsKeyBlocked(aKey) && ImGui::IsKeyPressed(aKey); }

	// ==========================================================================
	// Screen geometry of the last frame
	//
	// The glyph layout depends on the scroll offset, the gutter width and the
	// font metrics, all of which are only knowable from inside the editor's own
	// child window. These are therefore recorded while Render() runs, and read
	// afterwards by a host that has to place something next to the text, such as
	// a completion list or a signature hint. They are valid for the frame just
	// rendered, and before the first Render() they are zero.
	// ==========================================================================
	/// @brief Rectangle the editor's text area occupies on screen
	ImVec2 GetEditorScreenMin() const { return mEditorScreenMin; }
	ImVec2 GetEditorScreenMax() const { return mEditorScreenMax; }

	/// @brief Screen position of the left edge of the glyph at aPosition
	/// @details The exact inverse of ScreenToCoordinates, so a host can place an
	/// overlay under the glyph the cursor is on rather than at a guessed offset.
	ImVec2 CoordinatesToScreen(const Coordinates& aPosition) const;

	/// @brief Screen position of the cursor glyph, for the frame just rendered
	ImVec2 GetCursorScreenPos() const { return mCursorScreenPos; }

	/// @brief Distance between two text baselines, for laying out rows
	float GetLineHeight() const { return mCharAdvance.y; }

	/// @brief Distance the text area is scrolled, to convert a position within
	///        the buffer into one on screen
	float GetScrollX() const { return mScrollX; }
	float GetScrollY() const { return mScrollY; }

	void Render(const char* aTitle, const ImVec2& aSize = ImVec2(), bool aBorder = false);
	void SetText(const std::string& aText);
	std::string GetText() const;

	void SetTextLines(const std::vector<std::string>& aLines);
	std::vector<std::string> GetTextLines() const;

	std::string GetSelectedText() const;
	std::string GetCurrentLineText()const;

	int GetTotalLines() const { return (int)mLines.size(); }
	bool IsOverwrite() const { return mOverwrite; }

	void SetReadOnly(bool aValue);
	bool IsReadOnly() const { return mReadOnly; }
	bool IsTextChanged() const { return mTextChanged; }
	bool IsCursorPositionChanged() const { return mCursorPositionChanged; }

	bool IsColorizerEnabled() const { return mColorizerEnabled; }
	void SetColorizerEnable(bool aValue);

	Coordinates GetCursorPosition() const { return GetActualCursorCoordinates(); }
	void SetCursorPosition(const Coordinates& aPosition);

	inline void SetHandleMouseInputs    (bool aValue){ mHandleMouseInputs    = aValue;}
	inline bool IsHandleMouseInputsEnabled() const { return mHandleKeyboardInputs; }

	inline void SetHandleKeyboardInputs (bool aValue){ mHandleKeyboardInputs = aValue;}
	inline bool IsHandleKeyboardInputsEnabled() const { return mHandleKeyboardInputs; }

	inline void SetImGuiChildIgnored    (bool aValue){ mIgnoreImGuiChild     = aValue;}
	inline bool IsImGuiChildIgnored() const { return mIgnoreImGuiChild; }

	inline void SetShowWhitespaces(bool aValue) { mShowWhitespaces = aValue; }
	inline bool IsShowingWhitespaces() const { return mShowWhitespaces; }

	void SetTabSize(int aValue);
	inline int GetTabSize() const { return mTabSize; }

	void InsertText(const std::string& aValue);
	void InsertText(const char* aValue);

	void MoveUp(int aAmount = 1, bool aSelect = false);
	void MoveDown(int aAmount = 1, bool aSelect = false);
	void MoveLeft(int aAmount = 1, bool aSelect = false, bool aWordMode = false);
	void MoveRight(int aAmount = 1, bool aSelect = false, bool aWordMode = false);
	void MoveTop(bool aSelect = false);
	void MoveBottom(bool aSelect = false);
	void MoveHome(bool aSelect = false);
	void MoveEnd(bool aSelect = false);

	void SetSelectionStart(const Coordinates& aPosition);
	void SetSelectionEnd(const Coordinates& aPosition);
	void SetSelection(const Coordinates& aStart, const Coordinates& aEnd, SelectionMode aMode = SelectionMode::Normal);
	void SelectWordUnderCursor();
	void SelectAll();
	bool HasSelection() const;

	void Copy();
	void Cut();
	void Paste();
	void Delete();

	bool CanUndo() const;
	bool CanRedo() const;
	void Undo(int aSteps = 1);
	void Redo(int aSteps = 1);

	static const Palette& GetDarkPalette();
	static const Palette& GetLightPalette();
	static const Palette& GetRetroBluePalette();

private:
	typedef std::vector<std::pair<std::regex, PaletteIndex>> RegexList;

	struct EditorState
	{
		Coordinates mSelectionStart;
		Coordinates mSelectionEnd;
		Coordinates mCursorPosition;
	};

	class UndoRecord
	{
	public:
		UndoRecord() {}
		~UndoRecord() {}

		UndoRecord(
			const std::string& aAdded,
			const TextEditor::Coordinates aAddedStart,
			const TextEditor::Coordinates aAddedEnd,

			const std::string& aRemoved,
			const TextEditor::Coordinates aRemovedStart,
			const TextEditor::Coordinates aRemovedEnd,

			TextEditor::EditorState& aBefore,
			TextEditor::EditorState& aAfter);

		void Undo(TextEditor* aEditor);
		void Redo(TextEditor* aEditor);

		std::string mAdded;
		Coordinates mAddedStart;
		Coordinates mAddedEnd;

		std::string mRemoved;
		Coordinates mRemovedStart;
		Coordinates mRemovedEnd;

		EditorState mBefore;
		EditorState mAfter;
	};

	typedef std::vector<UndoRecord> UndoBuffer;

	void ProcessInputs();
	void Colorize(int aFromLine = 0, int aCount = -1);
	void ColorizeRange(int aFromLine = 0, int aToLine = 0);
	void ColorizeInternal();
	float TextDistanceToLineStart(const Coordinates& aFrom) const;
	void EnsureCursorVisible();
	int GetPageSize() const;
	std::string GetText(const Coordinates& aStart, const Coordinates& aEnd) const;
	Coordinates GetActualCursorCoordinates() const;
	Coordinates SanitizeCoordinates(const Coordinates& aValue) const;
	void Advance(Coordinates& aCoordinates) const;
	void DeleteRange(const Coordinates& aStart, const Coordinates& aEnd);
	int InsertTextAt(Coordinates& aWhere, const char* aValue);
	void AddUndo(UndoRecord& aValue);
	Coordinates ScreenPosToCoordinates(const ImVec2& aPosition) const;
	Coordinates FindWordStart(const Coordinates& aFrom) const;
	Coordinates FindWordEnd(const Coordinates& aFrom) const;
	Coordinates FindNextWord(const Coordinates& aFrom) const;
	int GetCharacterIndex(const Coordinates& aCoordinates) const;
	int GetCharacterColumn(int aLine, int aIndex) const;
	int GetLineCharacterCount(int aLine) const;
	int GetLineMaxColumn(int aLine) const;
	bool IsOnWordBoundary(const Coordinates& aAt) const;
	void RemoveLine(int aStart, int aEnd);
	void RemoveLine(int aIndex);
	Line& InsertLine(int aIndex);
	void EnterCharacter(ImWchar aChar, bool aShift);
	void Backspace();
	void DeleteSelection();
	std::string GetWordUnderCursor() const;
	std::string GetWordAt(const Coordinates& aCoords) const;
	ImU32 GetGlyphColor(const Glyph& aGlyph) const;

	void HandleKeyboardInputs();
	void HandleMouseInputs();
	void Render();

	float mLineSpacing;
	Lines mLines;
	EditorState mState;
	UndoBuffer mUndoBuffer;
	int mUndoIndex;

	int mTabSize;
	bool mOverwrite;
	bool mReadOnly;
	bool mWithinRender;
	bool mScrollToCursor;
	bool mScrollToTop;
	bool mTextChanged;
	bool mColorizerEnabled;
	float mTextStart;                   // position (in pixels) where a code line starts relative to the left of the TextEditor.
	int  mLeftMargin;
	bool mCursorPositionChanged;
	int mColorRangeMin, mColorRangeMax;
	SelectionMode mSelectionMode;
	bool mHandleKeyboardInputs;
	bool mHandleMouseInputs;
	bool mIgnoreImGuiChild;
	bool mShowWhitespaces;

	Palette mPaletteBase;
	Palette mPalette;
	LanguageDefinition mLanguageDefinition;
	RegexList mRegexList;

	bool mCheckComments;
	Breakpoints mBreakpoints;
	ErrorMarkers mErrorMarkers;
	struct SemanticSpan
	{
		int mColStart = 0;
		int mColEnd = 0;
		PaletteIndex mColor = PaletteIndex::Default;
	};
	std::map<int, std::vector<SemanticSpan>> mSemanticTokens;
	ImVec2 mCharAdvance;
	// Geometry of the frame last rendered, in-class initialised so it does not
	// disturb the constructor's initialiser order.
	ImVec2 mEditorScreenMin = ImVec2(0.0f, 0.0f);
	ImVec2 mEditorScreenMax = ImVec2(0.0f, 0.0f);
	ImVec2 mCursorScreenPos = ImVec2(0.0f, 0.0f);
	float mScrollX = 0.0f;
	float mScrollY = 0.0f;
	Coordinates mInteractiveStart, mInteractiveEnd;
	std::string mLineBuffer;
	uint64_t mStartTime;

	/// Keys the host took for the frame; see BlockKey(). In-class initialised, and
	/// declared last so it cannot disturb the constructor's initialiser order.
	enum { BlockedKeyWords = (ImGuiKey_NamedKey_COUNT + 31) / 32 };
	unsigned int mBlockedKeys[BlockedKeyWords] = {};

	float mLastClick;
};
