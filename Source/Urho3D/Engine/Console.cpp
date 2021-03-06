//
// Copyright (c) 2008-2022 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Engine/Console.h"
#include "../Engine/EngineEvents.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Graphics.h"
#include "../Input/Input.h"
#include "../IO/IOEvents.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"
#include "../UI/DropDownList.h"
#include "../UI/Font.h"
#include "../UI/LineEdit.h"
#include "../UI/ListView.h"
#include "../UI/ScrollBar.h"
#include "../UI/Text.h"
#include "../UI/UI.h"
#include "../UI/UIEvents.h"

#include "../DebugNew.h"

using namespace std;

namespace Urho3D
{
    static constexpr int DEFAULT_CONSOLE_ROWS = 16;
    static constexpr int DEFAULT_HISTORY_SIZE = 16;

    const char* logStyles[] =
    {
        "ConsoleTraceText",
        "ConsoleDebugText",
        "ConsoleInfoText",
        "ConsoleWarningText",
        "ConsoleErrorText",
        "ConsoleText"
    };

    Console::Console(Context* context) :
        Object(context),
        autoVisibleOnError_(false),
        historyRows_(DEFAULT_HISTORY_SIZE),
        historyPosition_(0),
        autoCompletePosition_(0),
        historyOrAutoCompleteChange_(false),
        printing_(false)
    {
        auto* ui = GetSubsystem<UI>();
        UIElement* uiRoot = ui->GetRoot();

        // By default prevent the automatic showing of the screen keyboard
        focusOnShow_ = !ui->GetUseScreenKeyboard();

        background_ = uiRoot->CreateChild<BorderImage>();
        background_->SetBringToBack(false);
        background_->SetClipChildren(true);
        background_->SetEnabled(true);
        background_->SetVisible(false); // Hide by default
        background_->SetPriority(200); // Show on top of the debug HUD
        background_->SetBringToBack(false);
        background_->SetLayout(LM_VERTICAL);

        rowContainer_ = background_->CreateChild<ListView>();
        rowContainer_->SetHighlightMode(HM_ALWAYS);
        rowContainer_->SetMultiselect(true);

        commandLine_ = background_->CreateChild<UIElement>();
        commandLine_->SetLayoutMode(LM_HORIZONTAL);
        commandLine_->SetLayoutSpacing(1);
        interpreters_ = commandLine_->CreateChild<DropDownList>();
        lineEdit_ = commandLine_->CreateChild<LineEdit>();
        lineEdit_->SetFocusMode(FM_FOCUSABLE);  // Do not allow defocus with ESC

        closeButton_ = uiRoot->CreateChild<Button>();
        closeButton_->SetVisible(false);
        closeButton_->SetPriority(background_->GetPriority() + 1);  // Show on top of console's background
        closeButton_->SetBringToBack(false);

        SetNumRows(DEFAULT_CONSOLE_ROWS);

        SubscribeToEvent(interpreters_, E_ITEMSELECTED, URHO3D_HANDLER(Console, HandleInterpreterSelected));
        SubscribeToEvent(lineEdit_, E_TEXTCHANGED, URHO3D_HANDLER(Console, HandleTextChanged));
        SubscribeToEvent(lineEdit_, E_TEXTFINISHED, URHO3D_HANDLER(Console, HandleTextFinished));
        SubscribeToEvent(lineEdit_, E_UNHANDLEDKEY, URHO3D_HANDLER(Console, HandleLineEditKey));
        SubscribeToEvent(closeButton_, E_RELEASED, URHO3D_HANDLER(Console, HandleCloseButtonPressed));
        SubscribeToEvent(uiRoot, E_RESIZED, URHO3D_HANDLER(Console, HandleRootElementResized));
        SubscribeToEvent(E_LOGMESSAGE, URHO3D_HANDLER(Console, HandleLogMessage));
        SubscribeToEvent(E_POSTUPDATE, URHO3D_HANDLER(Console, HandlePostUpdate));
    }

    Console::~Console()
    {
        background_->Remove();
        closeButton_->Remove();
    }

    void Console::SetDefaultStyle(XMLFile* style)
    {
        if (!style)
            return;

        background_->SetDefaultStyle(style);
        background_->SetStyle("ConsoleBackground");
        rowContainer_->SetStyleAuto();
        for (unsigned i = 0; i < rowContainer_->GetNumItems(); ++i)
            rowContainer_->GetItem(i)->SetStyle("ConsoleText");
        interpreters_->SetStyleAuto();
        for (unsigned i = 0; i < interpreters_->GetNumItems(); ++i)
            interpreters_->GetItem(i)->SetStyle("ConsoleText");
        lineEdit_->SetStyle("ConsoleLineEdit");

        closeButton_->SetDefaultStyle(style);
        closeButton_->SetStyle("CloseButton");

        UpdateElements();
    }

    void Console::SetVisible(bool enable)
    {
        auto* input = GetSubsystem<Input>();
        auto* ui = GetSubsystem<UI>();
        Cursor* cursor = ui->GetCursor();

        background_->SetVisible(enable);
        closeButton_->SetVisible(enable);

        if (enable)
        {
            // Check if we have receivers for E_CONSOLECOMMAND every time here in case the handler is being added later dynamically
            bool hasInterpreter = PopulateInterpreter();
            commandLine_->SetVisible(hasInterpreter);
            if (hasInterpreter && focusOnShow_)
                ui->SetFocusElement(lineEdit_);

            // Ensure the background has no empty space when shown without the lineedit
            background_->SetHeight(background_->GetMinHeight());

            if (!cursor)
            {
                // Show OS mouse
                input->SetMouseMode(MM_FREE, true);
                input->SetMouseVisible(true, true);
            }

            input->SetMouseGrabbed(false, true);
        }
        else
        {
            rowContainer_->SetFocus(false);
            interpreters_->SetFocus(false);
            lineEdit_->SetFocus(false);

            if (!cursor)
            {
                // Restore OS mouse visibility
                input->ResetMouseMode();
                input->ResetMouseVisible();
            }

            input->ResetMouseGrabbed();
        }
    }

    void Console::Toggle()
    {
        SetVisible(!IsVisible());
    }

    void Console::SetNumBufferedRows(unsigned rows)
    {
        if (rows < displayedRows_)
            return;

        rowContainer_->DisableLayoutUpdate();

        int delta = rowContainer_->GetNumItems() - rows;
        if (delta > 0)
        {
            // We have more, remove oldest rows first
            for (int i = 0; i < delta; ++i)
                rowContainer_->RemoveItem((unsigned)0);
        }
        else
        {
            // We have less, add more rows at the top
            for (int i = 0; i > delta; --i)
            {
                auto* text = new Text(context_);
                // If style is already set, apply here to ensure proper height of the console when
                // amount of rows is changed
                if (background_->GetDefaultStyle())
                    text->SetStyle("ConsoleText");
                rowContainer_->InsertItem(0, text);
            }
        }

        rowContainer_->EnsureItemVisibility(rowContainer_->GetItem(rowContainer_->GetNumItems() - 1));
        rowContainer_->EnableLayoutUpdate();
        rowContainer_->UpdateLayout();

        UpdateElements();
    }

    void Console::SetNumRows(unsigned rows)
    {
        if (!rows)
            return;

        displayedRows_ = rows;
        if (GetNumBufferedRows() < rows)
            SetNumBufferedRows(rows);

        UpdateElements();
    }

    void Console::SetNumHistoryRows(unsigned rows)
    {
        historyRows_ = rows;
        if (history.size() > rows)
            history.resize(rows);
        if (historyPosition_ > rows)
            historyPosition_ = rows;
    }

    void Console::SetFocusOnShow(bool enable)
    {
        focusOnShow_ = enable;
    }

    void Console::AddAutoComplete(const String& option)
    {
        // Sorted insertion
        auto iter = std::upper_bound(autoComplete_.begin(), autoComplete_.end(), option);
        if (iter == autoComplete_.end())
        {
            autoComplete_.push_back(option);
        }
        else if (iter == autoComplete_.begin() || *(iter - 1) != option)
        {
            // Make sure it isn't a duplicate
            autoComplete_.insert(iter, option);
        }
    }

    void Console::RemoveAutoComplete(const String& option)
    {
        // Erase and keep ordered
        autoComplete_.erase(std::lower_bound(autoComplete_.begin(), autoComplete_.end(), option));
        if (autoCompletePosition_ > autoComplete_.size())
            autoCompletePosition_ = (uint32_t)autoComplete_.size();
    }

    void Console::UpdateElements()
    {
        int width = GetSubsystem<UI>()->GetRoot()->GetWidth();
        const IntRect& border = background_->GetLayoutBorder();
        const IntRect& panelBorder = rowContainer_->GetScrollPanel()->GetClipBorder();
        rowContainer_->SetFixedWidth(width - border.left_ - border.right_);
        rowContainer_->SetFixedHeight(
            displayedRows_ * rowContainer_->GetItem((unsigned)0)->GetHeight() + panelBorder.top_ + panelBorder.bottom_ +
            (rowContainer_->GetHorizontalScrollBar()->IsVisible() ? rowContainer_->GetHorizontalScrollBar()->GetHeight() : 0));
        background_->SetFixedWidth(width);
        background_->SetHeight(background_->GetMinHeight());
    }

    XMLFile* Console::GetDefaultStyle() const
    {
        return background_->GetDefaultStyle(false);
    }

    bool Console::IsVisible() const
    {
        return background_ && background_->IsVisible();
    }

    unsigned Console::GetNumBufferedRows() const
    {
        return rowContainer_->GetNumItems();
    }

    void Console::CopySelectedRows() const
    {
        rowContainer_->CopySelectedItemsToClipboard();
    }

    const String& Console::GetHistoryRow(uint32_t index) const
    {
        return index < history.size() ? history[index] : String::EMPTY;
    }

    bool Console::PopulateInterpreter()
    {
        interpreters_->RemoveAllItems();

        EventReceiverGroup* group = context_->GetEventReceivers(E_CONSOLECOMMAND);
        if (!group || group->receivers.empty())
            return false;

        vector<String> names;
        for (size_t i = 0; i < group->receivers.size(); ++i)
        {
            Object* receiver = group->receivers[i];
            if (receiver)
            {
                names.push_back(receiver->GetTypeName());
            }
        }
        std::sort(names.begin(), names.end());

        unsigned selection = M_MAX_UNSIGNED;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const String& name = names[i];
            if (name == commandInterpreter_)
                selection = i;
            auto* text = new Text(context_);
            text->SetStyle("ConsoleText");
            text->SetText(name);
            interpreters_->AddItem(text);
        }

        const IntRect& border = interpreters_->GetPopup()->GetLayoutBorder();
        interpreters_->SetMaxWidth(interpreters_->GetListView()->GetContentElement()->GetWidth() + border.left_ + border.right_);
        bool enabled = interpreters_->GetNumItems() > 1;
        interpreters_->SetEnabled(enabled);
        interpreters_->SetFocusMode(enabled ? FM_FOCUSABLE_DEFOCUSABLE : FM_NOTFOCUSABLE);

        if (selection == M_MAX_UNSIGNED)
        {
            selection = 0;
            commandInterpreter_ = names[selection];
        }
        interpreters_->SetSelection(selection);

        return true;
    }

    void Console::HandleInterpreterSelected(StringHash eventType, VariantMap& eventData)
    {
        commandInterpreter_ = static_cast<Text*>(interpreters_->GetSelectedItem())->GetText();
        lineEdit_->SetFocus(true);
    }

    void Console::HandleTextChanged(StringHash eventType, VariantMap& eventData)
    {
        // Save the original line
        // Make sure the change isn't caused by auto complete or history
        if (!historyOrAutoCompleteChange_)
            autoCompleteLine_ = eventData[TextEntry::P_TEXT].GetString();

        historyOrAutoCompleteChange_ = false;
    }

    void Console::HandleTextFinished(StringHash eventType, VariantMap& eventData)
    {
        using namespace TextFinished;

        String line = lineEdit_->GetText();
        if (!line.Empty())
        {
            // Send the command as an event for script subsystem
            using namespace ConsoleCommand;

            SendEvent(E_CONSOLECOMMAND,
                P_COMMAND, line,
                P_ID, static_cast<Text*>(interpreters_->GetSelectedItem())->GetText());

            // Make sure the line isn't the same as the last one
            if (history.empty() || line != history.back())
            {
                // Store to history, then clear the lineedit
                history.push_back(line);
                if (history.size() > historyRows_)
                    history.erase(history.begin());
            }

            historyPosition_ = (uint32_t)history.size(); // Reset
            autoCompletePosition_ = (uint32_t)autoComplete_.size(); // Reset

            currentRow_.Clear();
            lineEdit_->SetText(currentRow_);
        }
    }

    void Console::HandleLineEditKey(StringHash eventType, VariantMap& eventData)
    {
        if (!historyRows_)
            return;

        using namespace UnhandledKey;

        bool changed = false;

        switch (eventData[P_KEY].GetInt())
        {
            case KEY_UP:
                if (autoCompletePosition_ == 0)
                    autoCompletePosition_ = (uint32_t)autoComplete_.size();

                if (autoCompletePosition_ < autoComplete_.size())
                {
                    // Search for auto completion that contains the contents of the line
                    for (--autoCompletePosition_; autoCompletePosition_ != M_MAX_UNSIGNED; --autoCompletePosition_)
                    {
                        const String& current = autoComplete_[autoCompletePosition_];
                        if (current.StartsWith(autoCompleteLine_))
                        {
                            historyOrAutoCompleteChange_ = true;
                            lineEdit_->SetText(current);
                            break;
                        }
                    }

                    // If not found
                    if (autoCompletePosition_ == M_MAX_UNSIGNED)
                    {
                        // Reset the position
                        autoCompletePosition_ = (uint32_t)autoComplete_.size();
                        // Reset history position
                        historyPosition_ = (uint32_t)history.size();
                    }
                }

                // If no more auto complete options and history options left
                if (autoCompletePosition_ == autoComplete_.size() && historyPosition_ > 0)
                {
                    // If line text is not a history, save the current text value to be restored later
                    if (historyPosition_ == history.size())
                        currentRow_ = lineEdit_->GetText();
                    // Use the previous option
                    --historyPosition_;
                    changed = true;
                }
                break;

            case KEY_DOWN:
                // If history options left
                if (historyPosition_ < history.size())
                {
                    // Use the next option
                    ++historyPosition_;
                    changed = true;
                }
                else
                {
                    // Loop over
                    if (autoCompletePosition_ >= autoComplete_.size())
                        autoCompletePosition_ = 0;
                    else
                        ++autoCompletePosition_; // If not starting over, skip checking the currently found completion

                    unsigned startPosition = autoCompletePosition_;

                    // Search for auto completion that contains the contents of the line
                    for (; autoCompletePosition_ < autoComplete_.size(); ++autoCompletePosition_)
                    {
                        const String& current = autoComplete_[autoCompletePosition_];
                        if (current.StartsWith(autoCompleteLine_))
                        {
                            historyOrAutoCompleteChange_ = true;
                            lineEdit_->SetText(current);
                            break;
                        }
                    }

                    // Continue to search the complete range
                    if (autoCompletePosition_ == autoComplete_.size())
                    {
                        for (autoCompletePosition_ = 0; autoCompletePosition_ != startPosition; ++autoCompletePosition_)
                        {
                            const String& current = autoComplete_[autoCompletePosition_];
                            if (current.StartsWith(autoCompleteLine_))
                            {
                                historyOrAutoCompleteChange_ = true;
                                lineEdit_->SetText(current);
                                break;
                            }
                        }
                    }
                }
                break;

            default: break;
        }

        if (changed)
        {
            historyOrAutoCompleteChange_ = true;
            // Set text to history option
            if (historyPosition_ < history.size())
                lineEdit_->SetText(history[historyPosition_]);
            else // restore the original line value before it was set to history values
            {
                lineEdit_->SetText(currentRow_);
                // Set the auto complete position according to the currentRow
                for (autoCompletePosition_ = 0; autoCompletePosition_ < autoComplete_.size(); ++autoCompletePosition_)
                    if (autoComplete_[autoCompletePosition_].StartsWith(currentRow_))
                        break;
            }
        }
    }

    void Console::HandleCloseButtonPressed(StringHash eventType, VariantMap& eventData)
    {
        SetVisible(false);
    }

    void Console::HandleRootElementResized(StringHash eventType, VariantMap& eventData)
    {
        UpdateElements();
    }

    void Console::HandleLogMessage(StringHash eventType, VariantMap& eventData)
    {
        // If printing a log message causes more messages to be logged (error accessing font), disregard them
        if (printing_)
            return;

        using namespace LogMessage;

        int level = eventData[P_LEVEL].GetInt();
        // The message may be multi-line, so split to rows in that case
        std::vector<String> rows = eventData[P_MESSAGE].GetString().Split('\n');

        for (unsigned i = 0; i < rows.size(); ++i)
            pendingRows.push_back(std::make_pair(level, rows[i]));

        if (autoVisibleOnError_ && level == LOG_ERROR && !IsVisible())
            SetVisible(true);
    }

    void Console::HandlePostUpdate(StringHash eventType, VariantMap& eventData)
    {
        // Ensure UI-elements are not detached
        if (!background_->GetParent())
        {
            auto* ui = GetSubsystem<UI>();
            UIElement* uiRoot = ui->GetRoot();
            uiRoot->AddChild(background_);
            uiRoot->AddChild(closeButton_);
        }

        if (!rowContainer_->GetNumItems() || pendingRows.empty())
            return;

        printing_ = true;
        rowContainer_->DisableLayoutUpdate();

        Text* text = nullptr;
        for (size_t i = 0; i < pendingRows.size(); ++i)
        {
            rowContainer_->RemoveItem((unsigned)0);
            text = new Text(context_);
            text->SetText(pendingRows[i].second);

            // Highlight console messages based on their type
            text->SetStyle(logStyles[pendingRows[i].first]);

            rowContainer_->AddItem(text);
        }

        pendingRows.clear();

        rowContainer_->EnsureItemVisibility(text);
        rowContainer_->EnableLayoutUpdate();
        rowContainer_->UpdateLayout();
        UpdateElements();   // May need to readjust the height due to scrollbar visibility changes
        printing_ = false;
    }
}
