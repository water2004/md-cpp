#include "pch.h"
#include "settings/ShortcutSettingsSerialization.h"

import folia.core.utf;

namespace
{
    using namespace folia::platform::editor;

    std::string ScopeName(EditorShortcutScope scope)
    {
        switch (scope)
        {
            case EditorShortcutScope::Global: return "global";
            case EditorShortcutScope::Code: return "code";
            case EditorShortcutScope::Math: return "math";
        }
        throw std::runtime_error("unsupported shortcut scope");
    }

    EditorShortcutScope ParseScope(winrt::hstring const& value)
    {
        if (value == L"global") return EditorShortcutScope::Global;
        if (value == L"code") return EditorShortcutScope::Code;
        if (value == L"math") return EditorShortcutScope::Math;
        throw std::runtime_error("unsupported shortcut scope");
    }

    winrt::Windows::Data::Json::JsonObject SerializeShortcut(EditorShortcutBinding const& binding)
    {
        using winrt::Windows::Data::Json::JsonValue;
        winrt::Windows::Data::Json::JsonObject value;
        value.Insert(L"id", JsonValue::CreateStringValue(winrt::to_hstring(binding.id)));
        value.Insert(L"actionId", JsonValue::CreateStringValue(winrt::to_hstring(binding.action_id)));
        value.Insert(L"customName", JsonValue::CreateStringValue(winrt::to_hstring(binding.custom_name)));
        value.Insert(L"actionKind", JsonValue::CreateStringValue(
            binding.action_kind == EditorShortcutActionKind::BuiltIn ? L"builtin" : L"snippet"));
        value.Insert(L"scope", JsonValue::CreateStringValue(winrt::to_hstring(ScopeName(binding.scope))));
        value.Insert(L"snippet", JsonValue::CreateStringValue(
            winrt::to_hstring(folia::cps_to_utf8(binding.snippet))));
        if (!binding.gesture)
        {
            value.Insert(L"gesture", JsonValue::CreateNullValue());
            return value;
        }

        winrt::Windows::Data::Json::JsonObject gesture;
        gesture.Insert(L"key", JsonValue::CreateNumberValue(
            static_cast<std::uint32_t>(binding.gesture->key)));
        gesture.Insert(L"control", JsonValue::CreateBooleanValue(binding.gesture->control));
        gesture.Insert(L"shift", JsonValue::CreateBooleanValue(binding.gesture->shift));
        gesture.Insert(L"alt", JsonValue::CreateBooleanValue(binding.gesture->alt));
        value.Insert(L"gesture", gesture);
        return value;
    }

    EditorShortcutBinding ParseShortcut(winrt::Windows::Data::Json::JsonObject const& value)
    {
        using winrt::Windows::Data::Json::JsonValueType;
        EditorShortcutBinding binding;
        binding.id = winrt::to_string(value.GetNamedString(L"id"));
        binding.action_id = winrt::to_string(value.GetNamedString(L"actionId"));
        binding.custom_name = winrt::to_string(value.GetNamedString(L"customName", L""));
        auto kind = value.GetNamedString(L"actionKind");
        if (kind == L"builtin") binding.action_kind = EditorShortcutActionKind::BuiltIn;
        else if (kind == L"snippet") binding.action_kind = EditorShortcutActionKind::InsertSnippet;
        else throw std::runtime_error("unsupported shortcut action kind");
        binding.scope = ParseScope(value.GetNamedString(L"scope"));
        binding.snippet = folia::utf8_to_cps(winrt::to_string(value.GetNamedString(L"snippet", L"")));
        auto gestureValue = value.GetNamedValue(L"gesture");
        if (gestureValue.ValueType() != JsonValueType::Null)
        {
            auto gesture = gestureValue.GetObject();
            auto key = gesture.GetNamedNumber(L"key");
            if (key < 0 || key > 255 || std::floor(key) != key)
                throw std::runtime_error("invalid shortcut key");
            binding.gesture = EditorKeyGesture{
                .key = static_cast<EditorKey>(static_cast<std::uint32_t>(key)),
                .control = gesture.GetNamedBoolean(L"control", false),
                .shift = gesture.GetNamedBoolean(L"shift", false),
                .alt = gesture.GetNamedBoolean(L"alt", false),
            };
        }
        if (binding.id.empty() || binding.action_id.empty())
            throw std::runtime_error("shortcut identifiers cannot be empty");
        if (binding.action_kind == EditorShortcutActionKind::InsertSnippet
            && (binding.custom_name.empty() || binding.snippet.empty()))
            throw std::runtime_error("custom shortcut name and snippet cannot be empty");
        return binding;
    }
}

namespace winrt::Folia
{
    Windows::Data::Json::JsonArray SerializeShortcutSettings(
        std::vector<folia::platform::editor::EditorShortcutBinding> const& bindings)
    {
        Windows::Data::Json::JsonArray values;
        for (auto const& binding : bindings) values.Append(SerializeShortcut(binding));
        return values;
    }

    std::vector<folia::platform::editor::EditorShortcutBinding> ParseShortcutSettings(
        Windows::Data::Json::JsonArray const& values)
    {
        using namespace folia::platform::editor;
        auto result = default_editor_shortcuts();
        std::unordered_set<std::string> seen;
        for (auto const& value : values)
        {
            auto parsed = ParseShortcut(value.GetObject());
            if (!seen.insert(parsed.id).second) throw std::runtime_error("duplicate shortcut id");
            auto existing = std::ranges::find(result, parsed.id, &EditorShortcutBinding::id);
            if (existing != result.end()) *existing = std::move(parsed);
            else result.push_back(std::move(parsed));
        }
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            if (!result[index].gesture) continue;
            if (find_editor_shortcut_conflict(
                result, *result[index].gesture, result[index].scope, index))
                throw std::runtime_error("conflicting shortcut gestures");
        }
        return result;
    }
}
