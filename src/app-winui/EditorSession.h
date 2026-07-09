#pragma once

namespace winrt::ElMd
{
    struct EditorSession
    {
        void Open(winrt::Windows::Storage::StorageFile const& file, winrt::hstring const& text);
        void SaveAs(winrt::Windows::Storage::StorageFile const& file);
        void SetText(winrt::hstring const& text);
        bool HasFile() const;
        winrt::Windows::Storage::StorageFile File() const;
        winrt::hstring Text() const;
        winrt::hstring DisplayName() const;
        winrt::hstring Path() const;
        uint64_t Revision() const;

    private:
        winrt::Windows::Storage::StorageFile file_{ nullptr };
        winrt::hstring text_;
        uint64_t revision_ = 0;
    };
}
