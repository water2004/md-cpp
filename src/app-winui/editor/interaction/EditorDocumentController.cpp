#include "pch.h"
#include "editor/interaction/EditorDocumentController.h"

import elmd.core.command;
import elmd.core.utf;

namespace winrt::ElMd
{
    struct EditorDocumentController::State
    {
        std::atomic_uint64_t generation = 1;
        EditorSession* session = nullptr;
        EditorSurfaceRenderer* renderer = nullptr;
        EditorTextInputController* textInput = nullptr;
        ExecuteCommand executeCommand;
        SetStatus setStatus;
        DocumentChanged documentChanged;
        Render render;
        WindowHandle windowHandle;
    };

    EditorDocumentController::EditorDocumentController() : state_(std::make_shared<State>())
    {
    }

    EditorDocumentController::~EditorDocumentController()
    {
        Detach();
    }

    void EditorDocumentController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        ExecuteCommand executeCommand,
        SetStatus setStatus,
        DocumentChanged documentChanged,
        Render render,
        WindowHandle windowHandle)
    {
        Detach();
        state_->session = &session;
        state_->renderer = &renderer;
        state_->textInput = &textInput;
        state_->executeCommand = std::move(executeCommand);
        state_->setStatus = std::move(setStatus);
        state_->documentChanged = std::move(documentChanged);
        state_->render = std::move(render);
        state_->windowHandle = std::move(windowHandle);
    }

    void EditorDocumentController::Detach()
    {
        state_->generation.fetch_add(1);
        state_->session = nullptr;
        state_->renderer = nullptr;
        state_->textInput = nullptr;
        state_->executeCommand = {};
        state_->setStatus = {};
        state_->documentChanged = {};
        state_->render = {};
        state_->windowHandle = {};
    }

    void EditorDocumentController::OpenDocument()
    {
        OpenDocumentAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::SaveDocument()
    {
        SaveDocumentAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::SaveDocumentAs()
    {
        SaveDocumentAsAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::ExportPdf()
    {
        ExportPdfAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::InsertImage()
    {
        InsertImageAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::OpenLink(std::string href)
    {
        while (!href.empty() && static_cast<unsigned char>(href.front()) <= 0x20) href.erase(href.begin());
        while (!href.empty() && static_cast<unsigned char>(href.back()) <= 0x20) href.pop_back();
        if (href.empty() || !state_->session) return;
        if (href.front() == '#')
        {
            if (auto item = state_->session->RenderModel().outline.find_item_by_slug(href.substr(1)))
            {
                state_->session->SetSelection(item->position, item->position);
                if (state_->textInput) state_->textInput->NotifySelectionChanged();
                if (state_->renderer) state_->renderer->ScrollToPosition(item->position);
                if (state_->render) state_->render();
            }
            return;
        }
        OpenLinkAsync(state_, state_->generation.load(), std::move(href));
    }

    void EditorDocumentController::CopySelection()
    {
        if (!state_->session || !state_->session->HasSelection()) return;
        auto package = winrt::Windows::ApplicationModel::DataTransfer::DataPackage();
        package.SetText(winrt::to_hstring(state_->session->SelectedTextUtf8()));
        winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        if (state_->setStatus) state_->setStatus(L"Copied selection");
    }

    void EditorDocumentController::CutSelection()
    {
        if (!state_->session || !state_->session->HasSelection()) return;
        CopySelection();
        elmd::Command command;
        command.kind = elmd::CommandKind::DeleteSelection;
        if (state_->executeCommand) state_->executeCommand(command);
    }

    void EditorDocumentController::PasteClipboard()
    {
        PasteClipboardAsync(state_, state_->generation.load());
    }

    bool EditorDocumentController::Active(std::shared_ptr<State> const& state, std::uint64_t generation)
    {
        return state && state->generation.load() == generation && state->session;
    }

    winrt::fire_and_forget EditorDocumentController::OpenDocumentAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            picker.FileTypeFilter().Append(L".md");
            picker.FileTypeFilter().Append(L".markdown");
            picker.FileTypeFilter().Append(L".txt");
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSingleFileAsync();
            if (!Active(state, generation)) co_return;
            if (!file)
            {
                if (state->setStatus) state->setStatus(L"Open cancelled");
                co_return;
            }
            auto text = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            if (!Active(state, generation)) co_return;
            if (state->setStatus) state->setStatus(L"Opening " + file.Name() + L"…");
            winrt::apartment_context uiContext;
            co_await winrt::resume_background();
            EditorSession loaded;
            loaded.Open(file, text);
            co_await uiContext;
            if (!Active(state, generation)) co_return;
            *state->session = std::move(loaded);
            if (state->renderer)
            {
                state->renderer->ResetDocumentCaches();
                state->renderer->SetScrollOffset(0.0f);
            }
            if (state->textInput) state->textInput->NotifyTextChanged();
            if (state->documentChanged) state->documentChanged();
            if (state->setStatus) state->setStatus(L"Opened " + file.Name());
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"Open failed: " + error.message());
        }
    }

    winrt::fire_and_forget EditorDocumentController::SaveDocumentAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation)) co_return;
            if (!state->session->HasFile())
            {
                SaveDocumentAsAsync(state, generation);
                co_return;
            }
            auto file = state->session->File();
            auto text = state->session->Text();
            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, text);
            if (!Active(state, generation)) co_return;
            if (state->setStatus) state->setStatus(L"Saved " + file.Name());
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"Save failed: " + error.message());
        }
    }

    winrt::fire_and_forget EditorDocumentController::SaveDocumentAsAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileSavePicker();
            picker.DefaultFileExtension(L".md");
            picker.SuggestedFileName(L"Untitled.md");
            picker.FileTypeChoices().Insert(L"Markdown", winrt::single_threaded_vector<winrt::hstring>({ L".md" }));
            picker.FileTypeChoices().Insert(L"Text", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSaveFileAsync();
            if (!Active(state, generation)) co_return;
            if (!file)
            {
                if (state->setStatus) state->setStatus(L"Save cancelled");
                co_return;
            }
            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, state->session->Text());
            if (!Active(state, generation)) co_return;
            state->session->SaveAs(file);
            if (state->documentChanged) state->documentChanged();
            if (state->setStatus) state->setStatus(L"Saved " + file.Name());
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"Save failed: " + error.message());
        }
    }

    winrt::fire_and_forget EditorDocumentController::ExportPdfAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle || !state->renderer) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileSavePicker();
            picker.DefaultFileExtension(L".pdf");
            auto displayName = state->session->DisplayName();
            auto suggested = std::filesystem::path(displayName.c_str()).stem().wstring();
            picker.SuggestedFileName(suggested.empty() ? L"Untitled.pdf" : suggested + L".pdf");
            picker.FileTypeChoices().Insert(L"PDF document", winrt::single_threaded_vector<winrt::hstring>({L".pdf"}));
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSaveFileAsync();
            if (!Active(state, generation)) co_return;
            if (!file)
            {
                if (state->setStatus) state->setStatus(L"PDF export cancelled");
                co_return;
            }

            // Export the exact document snapshot that existed when the picker
            // closed. Asset preparation may yield to the UI thread, but later
            // edits must not leak into this print job.
            auto renderModel = state->session->BuildPrintRenderModel();
            auto baseDirectory = state->session->BaseDirectory();
            auto title = std::filesystem::path(displayName.c_str()).stem().wstring();
            detail::EditorRenderFrame frame{renderModel, {}, baseDirectory, {}, {}};
            auto outputPath = std::filesystem::path(file.Path().c_str());
            if (state->setStatus) state->setStatus(L"Preparing PDF…");
            winrt::apartment_context uiContext;
            for (;;)
            {
                if (!Active(state, generation)) co_return;
                auto result = state->renderer->ExportPdf(outputPath, title, frame);
                if (result == EditorSurfaceRenderer::PdfExportResult::Completed) break;
                if (state->setStatus) state->setStatus(L"Preparing PDF assets…");
                co_await winrt::resume_after(std::chrono::milliseconds(80));
                co_await uiContext;
            }
            if (Active(state, generation) && state->setStatus)
                state->setStatus(L"Exported PDF: " + file.Path());
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"PDF export failed: " + error.message());
        }
        catch (std::exception const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"PDF export failed: " + winrt::to_hstring(error.what()));
        }
    }

    winrt::fire_and_forget EditorDocumentController::InsertImageAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            for (auto extension : { L".png", L".jpg", L".jpeg", L".gif", L".webp", L".bmp" }) picker.FileTypeFilter().Append(extension);
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSingleFileAsync();
            if (!Active(state, generation) || !file) co_return;
            std::filesystem::path path(file.Path().c_str());
            if (state->session->HasFile())
            {
                std::error_code error;
                auto base = std::filesystem::path(state->session->Path().c_str()).parent_path();
                auto relative = std::filesystem::relative(path, base, error);
                if (!error && !relative.empty()) path = std::move(relative);
            }
            elmd::Command command;
            command.kind = elmd::CommandKind::InsertImage;
            command.path = elmd::utf8_to_cps(winrt::to_string(winrt::hstring(path.generic_wstring())));
            if (state->executeCommand) state->executeCommand(command);
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"Image insert failed: " + error.message());
        }
    }

    winrt::fire_and_forget EditorDocumentController::OpenLinkAsync(std::shared_ptr<State> state, std::uint64_t generation, std::string href)
    {
        auto lower = href;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        try
        {
            if (!Active(state, generation)) co_return;
            if (lower.starts_with("https://") || lower.starts_with("http://") || lower.starts_with("mailto:"))
            {
                co_await winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(href)));
                co_return;
            }
            auto colon = lower.find(':');
            auto boundary = lower.find_first_of("/?#");
            if (colon != std::string::npos && (boundary == std::string::npos || colon < boundary)) co_return;
            auto path = std::filesystem::path(winrt::to_hstring(href).c_str());
            if (path.is_relative()) path = std::filesystem::path(state->session->BaseDirectory()) / path;
            auto file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path.lexically_normal().wstring());
            if (!Active(state, generation)) co_return;
            co_await winrt::Windows::System::Launcher::LaunchFileAsync(file);
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"Open link failed: " + error.message());
        }
    }

    winrt::fire_and_forget EditorDocumentController::PasteClipboardAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation)) co_return;
            auto content = winrt::Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
            if (!content.Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text())) co_return;
            auto text = co_await content.GetTextAsync();
            if (!Active(state, generation) || text.empty()) co_return;
            if (state->executeCommand)
            {
                elmd::Command command;
                command.kind = elmd::CommandKind::Paste;
                command.text = elmd::utf8_to_cps(winrt::to_string(text));
                state->executeCommand(command);
            }
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus) state->setStatus(L"Paste failed: " + error.message());
        }
    }
}
