#pragma once

namespace winrt::Folia
{
    enum class SyntaxHighlightKind
    {
        None,
        Keyword,
        Type,
        Function,
        String,
        Number,
        Comment,
        Operator,
        Preprocessor,
        Property,
        Constant,
    };

    struct SyntaxHighlightRange
    {
        std::uint32_t start = 0;
        std::uint32_t length = 0;
        SyntaxHighlightKind kind = SyntaxHighlightKind::None;
    };

    class TreeSitterHighlighter
    {
    public:
        TreeSitterHighlighter();
        ~TreeSitterHighlighter();
        TreeSitterHighlighter(TreeSitterHighlighter const&) = delete;
        TreeSitterHighlighter& operator=(TreeSitterHighlighter const&) = delete;

        std::vector<SyntaxHighlightRange> Highlight(std::string_view language, std::string_view source);
        void Clear();

    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
