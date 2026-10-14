/*
 * This file is part of Notepad Next.
 * Copyright 2021 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <QApplication>

#include "ApplicationSettings.h"

#include "EditorManager.h"
#include "ScintillaNext.h"
#include "Scintilla.h"

// Editor decorators
#include "BraceMatch.h"
#include "HighlightedScrollBar.h"
#include "SmartHighlighter.h"
#include "SurroundSelection.h"
#include "LineNumbers.h"
#include "BetterMultiSelection.h"
#include "AutoIndentation.h"
#include "AutoCompletion.h"
#include "URLFinder.h"
#include "BookMarkDecorator.h"
#include "HTMLAutoCompleteDecorator.h"


const int MARK_HIDELINESBEGIN = 23;
const int MARK_HIDELINESEND = 22;
const int MARK_HIDELINESUNDERLINE = 21;


EditorManager::EditorManager(ApplicationSettings *settings, QObject *parent)
    : QObject(parent), settings(settings)
{
    connect(this, &EditorManager::editorCreated, this, [=, this](ScintillaNext *editor) {
        connect(editor, &ScintillaNext::closed, this, [=, this]() {
            emit editorClosed(editor);
        });
    });

    connect(settings, &ApplicationSettings::showWrapSymbolChanged, this, [=, this](bool b) {
        for (auto &editor : getEditors()) {
            editor->setWrapVisualFlags(b ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
        }
    });


    connect(settings, &ApplicationSettings::showWhitespaceChanged, this, [=, this](bool b) {
        // TODO: could make SCWS_VISIBLEALWAYS configurable via settings. Probably not worth
        // taking up menu space e.g. show all, show leading, show trailing
        for (auto &editor : getEditors()) {
            editor->setViewWS(b ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
        }
    });

    connect(settings, &ApplicationSettings::showEndOfLineChanged, this, [=, this](bool b) {
        for (auto &editor : getEditors()) {
            editor->setViewEOL(b);
        }
    });

    connect(settings, &ApplicationSettings::showIndentGuideChanged, this, [=, this](bool b) {
        for (auto &editor : getEditors()) {
            editor->setIndentationGuides(b ? SC_IV_LOOKBOTH : SC_IV_NONE);
        }
    });

    connect(settings, &ApplicationSettings::wordWrapChanged, this, [=, this](bool b) {
        if (b) {
            for (auto &editor : getEditors()) {
                editor->setWrapMode(SC_WRAP_WORD);
            }
        }
        else {
            for (auto &editor : getEditors()) {
                // Store the top line and restore it after the lines have been unwrapped
                int topLine = editor->docLineFromVisible(editor->firstVisibleLine());
                editor->setWrapMode(SC_WRAP_NONE);
                editor->setFirstVisibleLine(topLine);
            }
        }
    });

    connect(settings, &ApplicationSettings::fontNameChanged, this, [=, this](QString fontName){
        for (auto &editor : getEditors()) {
            for (int i = 0; i <= STYLE_MAX; ++i) {
                editor->styleSetFont(i, fontName.toUtf8().data());
            }
        }
    });

    connect(settings, &ApplicationSettings::fontSizeChanged, this, [=, this](int fontSize){
        for (auto &editor : getEditors()) {
            for (int i = 0; i <= STYLE_MAX; ++i) {
                editor->styleSetSize(i, fontSize);
            }
        }
    });

    connect(settings, &ApplicationSettings::urlHighlightingChanged, this, [=, this](bool b){
        for (auto &editor : getEditors()) {
            URLFinder *decorator = editor->findChild<URLFinder *>(QString(), Qt::FindDirectChildrenOnly);
            if (decorator) {
                decorator->setEnabled(b);
            }
        }
    });

    connect(settings, &ApplicationSettings::showLineNumbersChanged, this, [=, this](bool b){
        for (auto &editor : getEditors()) {
            LineNumbers *decorator = editor->findChild<LineNumbers *>(QString(), Qt::FindDirectChildrenOnly);
            if (decorator) {
                decorator->setEnabled(b);
            }
        }
    });

    connect(settings, &ApplicationSettings::autoCompletionChanged, this, [=, this](bool b){
        for (auto &editor : getEditors()) {
            AutoCompletion *decorator = editor->findChild<AutoCompletion *>(QString(), Qt::FindDirectChildrenOnly);
            if (decorator) {
                decorator->setEnabled(b);
                if (!b && editor->autoCActive())
                    editor->autoCCancel();
            }
        }
    });
}

ScintillaNext *EditorManager::createEditor(const QString &name)
{
    ScintillaNext *editor = new ScintillaNext(name);

    manageEditor(editor);

    return editor;
}

ScintillaNext *EditorManager::createEditorFromFile(const QString &filePath, bool tryToCreate)
{
    ScintillaNext *editor = ScintillaNext::fromFile(filePath, tryToCreate);

    if (editor) {
        manageEditor(editor);
    }

    return editor;
}

ScintillaNext *EditorManager::getEditorByFilePath(const QString &filePath)
{
    QFileInfo newInfo(filePath);
    newInfo.makeAbsolute();

    purgeOldEditorPointers();

    for (ScintillaNext *editor : qAsConst(editors)) {
        if (editor->isFile() && editor->getFileInfo() == newInfo) {
            return editor;
        }
    }

    return Q_NULLPTR;
}

void EditorManager::manageEditor(ScintillaNext *editor)
{
    editors.append(QPointer<ScintillaNext>(editor));

    setupEditor(editor);

    emit editorCreated(editor);
}

#ifdef Q_OS_MACOS
// Scintilla's macOS key bindings are gated on PLAT_GTK_MACOSX (see KeyMap.cxx), which is
// never defined for the Qt platform layer, so they never take effect. Scintilla also routes
// word-wise movement through SCI_META, a modifier that the Qt layer does not report at all.
// The bindings are assigned here rather than by patching the bundled Scintilla so that
// updating Scintilla cannot silently revert them.
//
// On macOS Qt reports Command as Qt::ControlModifier (i.e. SCMOD_CTRL) and Option as
// Qt::AltModifier (SCMOD_ALT).
static void setupMacOsKeys(ScintillaNext *editor)
{
    constexpr int Cmd = SCMOD_CTRL;
    constexpr int Opt = SCMOD_ALT;
    constexpr int Shift = SCMOD_SHIFT;

    struct KeyBinding {
        int key;
        int modifiers;
        int message;
    };

    constexpr KeyBinding bindings[] = {
        // Command + arrow keys move to the start/end of the line and of the document
        {SCK_LEFT,  Cmd,               SCI_VCHOME},
        {SCK_LEFT,  Cmd | Shift,       SCI_VCHOMEEXTEND},
        {SCK_RIGHT, Cmd,               SCI_LINEEND},
        {SCK_RIGHT, Cmd | Shift,       SCI_LINEENDEXTEND},
        {SCK_UP,    Cmd,               SCI_DOCUMENTSTART},
        {SCK_DOWN,  Cmd,               SCI_DOCUMENTEND},

        // Command + Shift + Up/Down are deliberately left alone. They are already used by
        // the Move Selected Lines Up/Down actions, and those actions receive the key press
        // before the editor does.

        // Option + arrow keys move by word
        {SCK_LEFT,  Opt,               SCI_WORDLEFT},
        {SCK_LEFT,  Opt | Shift,       SCI_WORDLEFTEXTEND},
        {SCK_RIGHT, Opt,               SCI_WORDRIGHT},
        {SCK_RIGHT, Opt | Shift,       SCI_WORDRIGHTEXTEND},

        // Option + Shift + Left/Right now select by word, so add Command + Option + Shift +
        // arrow keys for rectangular selection. Option + Shift + Up/Down are left with
        // Scintilla's default rectangular behaviour since nothing else uses them.
        {SCK_LEFT,  Cmd | Opt | Shift, SCI_CHARLEFTRECTEXTEND},
        {SCK_RIGHT, Cmd | Opt | Shift, SCI_CHARRIGHTRECTEXTEND},
        {SCK_UP,    Cmd | Opt | Shift, SCI_LINEUPRECTEXTEND},
        {SCK_DOWN,  Cmd | Opt | Shift, SCI_LINEDOWNRECTEXTEND},

        // Deleting by word/to the start of the line, and redo
        {SCK_BACK,  Opt,               SCI_DELWORDLEFT},
        {SCK_BACK,  Cmd,               SCI_DELLINELEFT},
        {'Z',       Cmd | Shift,       SCI_REDO},
    };

    for (const KeyBinding &binding : bindings) {
        editor->assignCmdKey(binding.key + (binding.modifiers << 16), binding.message);
    }
}
#endif

void EditorManager::setupEditor(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    editor->clearCmdKey(SCK_INSERT);

#ifdef Q_OS_MACOS
    setupMacOsKeys(editor);
#endif

    editor->setFoldMarkers(QStringLiteral("box"));

    editor->setScaleTechnique(SCALE_TECHNIQUE_PIXEL_ALIGNED);

    editor->setIdleStyling(SC_IDLESTYLING_TOVISIBLE);
    editor->setEndAtLastLine(false);

    editor->setMultipleSelection(true);
    editor->setAdditionalSelectionTyping(true);
    editor->setMultiPaste(SC_MULTIPASTE_EACH);
    editor->setVirtualSpaceOptions(SCVS_RECTANGULARSELECTION);

    editor->setMarginLeft(2);

    editor->setMarginWidthN(0, 30);
    editor->setMarginMaskN(1, (1<<MARK_HIDELINESBEGIN) | (1<<MARK_HIDELINESEND) | (1<<MARK_HIDELINESUNDERLINE));
    editor->setMarginMaskN(2, SC_MASK_FOLDERS);
    editor->setMarginWidthN(2, 14);

    editor->markerDefine(MARK_HIDELINESUNDERLINE, SC_MARK_UNDERLINE);
    editor->markerSetBack(MARK_HIDELINESUNDERLINE, 0x77CC77);

    editor->markerDefine(MARK_HIDELINESBEGIN, SC_MARK_ARROW);
    editor->markerDefine(MARK_HIDELINESEND, SC_MARK_ARROWDOWN);

    editor->setMarginSensitiveN(2, true);

    editor->setFoldFlags(SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    editor->setScrollWidthTracking(true);
    editor->setScrollWidth(1);

    editor->setTabDrawMode(SCTD_STRIKEOUT);
    editor->setTabWidth(4);
    editor->setBackSpaceUnIndents(true);

    editor->setCaretLineVisible(true);
    editor->setCaretLineVisibleAlways(true);
    editor->setCaretWidth(2);

    // https://www.scintilla.org/ScintillaDoc.html#ElementColours
    // SC_ELEMENT_SELECTION_TEXT
    // SC_ELEMENT_SELECTION_BACK
    // SC_ELEMENT_SELECTION_ADDITIONAL_TEXT
    // SC_ELEMENT_SELECTION_ADDITIONAL_BACK
    // SC_ELEMENT_SELECTION_SECONDARY_TEXT
    // SC_ELEMENT_SELECTION_SECONDARY_BACK
    // SC_ELEMENT_SELECTION_INACTIVE_TEXT
    // SC_ELEMENT_CARET
    // SC_ELEMENT_CARET_ADDITIONAL
    // SC_ELEMENT_WHITE_SPACE_BACK
    // SC_ELEMENT_HOT_SPOT_ACTIVE
    // SC_ELEMENT_HOT_SPOT_ACTIVE_BACK
    // SC_ELEMENT_HIDDEN_LINE

    editor->setWhitespaceSize(2);

    editor->setAutomaticFold(SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    editor->markerEnableHighlight(true);

    editor->setCharsDefault();
    editor->setWordChars(editor->wordChars() + settings->additionalWordChars().toLatin1());

    applyTheme(editor);

    editor->setViewWS(settings->showWhitespace() ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    editor->setViewEOL(settings->showEndOfLine());
    editor->setWrapVisualFlags(settings->showWrapSymbol() ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
    editor->setIndentationGuides(settings->showIndentGuide() ? SC_IV_LOOKBOTH : SC_IV_NONE);
    editor->setWrapMode(settings->wordWrap() ? SC_WRAP_WORD : SC_WRAP_NONE);

    int detectedEOLMode = detectEOLMode(editor);
    if (detectedEOLMode == -1) {
        if (!settings->defaultEOLMode().isEmpty()) {
            const int eol = ScintillaNext::stringToEolMode(settings->defaultEOLMode().toLower());

            if (eol != -1)
                editor->setEOLMode(eol);
            else
                qWarning("Unexpected DefaultEOLMode: %s", qUtf8Printable(settings->defaultEOLMode()));
        }
        // else it will just stay whatever EOL mode it was when it was created
    }
    else {
        editor->setEOLMode(detectedEOLMode);
    }

    // Decorators
    SmartHighlighter *s = new SmartHighlighter(editor);
    s->setEnabled(true);

    HighlightedScrollBarDecorator *h = new HighlightedScrollBarDecorator(editor);
    h->setEnabled(true);

    BraceMatch *b = new BraceMatch(editor);
    b->setEnabled(true);

    LineNumbers *l = new LineNumbers(editor);
    l->setEnabled(settings->showLineNumbers());

    SurroundSelection *ss = new SurroundSelection(editor);
    ss->setEnabled(true);

    BetterMultiSelection *bms = new BetterMultiSelection(editor);
    bms->setEnabled(true);

    AutoIndentation *ai = new AutoIndentation(editor);
    ai->setEnabled(true);

    AutoCompletion *ac = new AutoCompletion(editor);
    ac->setEnabled(settings->autoCompletion());

    URLFinder *uf = new URLFinder(editor);
    uf->setEnabled(settings->urlHighlighting());

    BookMarkDecorator *bm = new BookMarkDecorator(editor);
    bm->setEnabled(true);

    new HTMLAutoCompleteDecorator(editor);
}

void EditorManager::applyTheme(ScintillaNext *editor)
{
    // Colors are Scintilla colors (0xBBGGRR) for styles/markers and 0xAABBGGRR for elements
    struct Palette {
        int defaultFore, defaultBack;
        int lineNumberFore, lineNumberBack;
        int braceLightFore, braceBadFore;
        int indentGuideFore;
        int edge;
        int foldMarginBack, foldMarginHi;
        int foldMarkerFore, foldMarkerBack, foldMarkerSelected;
        int selectionInactiveBack, caretLineBack, whiteSpace, foldLine;
    };

    static const Palette light = {
        0x000000, 0xFFFFFF,
        0x808080, 0xE4E4E4,
        0x0000FF, 0x000080,
        0xC0C0C0,
        0x80FFFF,
        0xFFFFFF, 0xE9E9E9,
        0xF3F3F3, 0x808080, 0x0000FF,
        static_cast<int>(0xFFE0E0E0), static_cast<int>(0xFFFFE8E8), static_cast<int>(0xFFD0D0D0), static_cast<int>(0xFFA0A0A0),
    };

    static const Palette dark = {
        0xD4D4D4, 0x1E1E1E,
        0x858585, 0x252526,
        0x50C8FF, 0x5050FF,
        0x404040,
        0x505050,
        0x1E1E1E, 0x2A2A2A,
        0x1E1E1E, 0xA0A0A0, 0xFFB070,
        static_cast<int>(0xFF404040), static_cast<int>(0xFF2D2D2D), static_cast<int>(0xFF505050), static_cast<int>(0xFF606060),
    };

    const bool isDark = settings->darkMode();
    const Palette &p = isDark ? dark : light;

    for (int i = SC_MARKNUM_FOLDEREND; i <= SC_MARKNUM_FOLDEROPEN; ++i) {
        editor->markerSetFore(i, p.foldMarkerFore);
        editor->markerSetBack(i, p.foldMarkerBack);
        editor->markerSetBackSelected(i, p.foldMarkerSelected);
    }

    editor->setEdgeColour(p.edge);

    editor->setElementColour(SC_ELEMENT_SELECTION_INACTIVE_BACK, p.selectionInactiveBack);
    editor->setElementColour(SC_ELEMENT_CARET_LINE_BACK, p.caretLineBack);
    editor->setElementColour(SC_ELEMENT_WHITE_SPACE, p.whiteSpace);
    editor->setElementColour(SC_ELEMENT_FOLD_LINE, p.foldLine);

    if (isDark) {
        editor->setElementColour(SC_ELEMENT_SELECTION_BACK, static_cast<int>(0xFF784F26));
        editor->setElementColour(SC_ELEMENT_CARET, static_cast<int>(0xFFFFFFFF));
    }
    else {
        // Go back to the Scintilla defaults
        editor->resetElementColour(SC_ELEMENT_SELECTION_BACK);
        editor->resetElementColour(SC_ELEMENT_CARET);
    }

    editor->setFoldMarginColour(true, p.foldMarginBack);
    editor->setFoldMarginHiColour(true, p.foldMarginHi);

    editor->styleSetFore(STYLE_DEFAULT, p.defaultFore);
    editor->styleSetBack(STYLE_DEFAULT, p.defaultBack);
    editor->styleSetSize(STYLE_DEFAULT, settings->fontSize());
    editor->styleSetFont(STYLE_DEFAULT, settings->fontName().toUtf8().data());
    editor->styleClearAll();

    editor->styleSetFore(STYLE_LINENUMBER, p.lineNumberFore);
    editor->styleSetBack(STYLE_LINENUMBER, p.lineNumberBack);
    editor->styleSetBold(STYLE_LINENUMBER, false);

    editor->styleSetFore(STYLE_BRACELIGHT, p.braceLightFore);
    editor->styleSetBack(STYLE_BRACELIGHT, p.defaultBack);

    editor->styleSetFore(STYLE_BRACEBAD, p.braceBadFore);
    editor->styleSetBack(STYLE_BRACEBAD, p.defaultBack);

    editor->styleSetFore(STYLE_INDENTGUIDE, p.indentGuideFore);
    editor->styleSetBack(STYLE_INDENTGUIDE, p.defaultBack);

    // STYLE_CONTROLCHAR
    // STYLE_CALLTIP
    // STYLE_FOLDDISPLAYTEXT
}

void EditorManager::purgeOldEditorPointers()
{
    QMutableListIterator<QPointer<ScintillaNext>> it(editors);

    while (it.hasNext()) {
        QPointer<ScintillaNext> pointer = it.next();
        if (pointer.isNull())
            it.remove();
    }
}

QList<QPointer<ScintillaNext> > EditorManager::getEditors()
{
    purgeOldEditorPointers();
    return editors;
}

int EditorManager::detectEOLMode(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    const int MIN_LINE_THRESHOLD = 3;
    const int MAX_BYTES_TO_CHECK = 10*1024;

    int index = 0;
    int lf = 0;
    int cr = 0;
    int crlf = 0;
    int chPrev = ' ';
    int chNext = editor->charAt(index);

    for (int i = 0; i < qMin(MAX_BYTES_TO_CHECK, (int) editor->length()); ++i) {
        int ch = chNext;
        chNext = editor->charAt(i + 1);

        if (ch == '\r') {
            if (chNext == '\n') crlf++;
            else cr++;
        }
        else if (ch == '\n') {
            if (chPrev != '\r') lf++;
        }

        chPrev = ch;

        // If any meet some minimum threshold then just declare victory
        if (crlf == MIN_LINE_THRESHOLD) return SC_EOL_CRLF;
        else if (cr == MIN_LINE_THRESHOLD) return SC_EOL_CR;
        else if (lf == MIN_LINE_THRESHOLD) return SC_EOL_LF;
    }

    // There are either no lines or only a few, so make a best effort determination

    if (crlf > cr && crlf > lf) {
        return SC_EOL_CRLF;
    }
    else if (cr > lf) {
        return SC_EOL_CR;
    }
    else if (lf > cr) {
        return SC_EOL_LF;
    }
    else {
        return -1;
    }
}
