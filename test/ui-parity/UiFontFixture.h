/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include "UiFixtures.h"

#include <filesystem>
#include <fstream>
#include <openrct2/Context.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Crypt.h>
#include <openrct2/core/UTF8.h>
#include <openrct2/drawing/Drawing.String.h>
#include <openrct2/drawing/Font.h>
#include <openrct2/drawing/TTF.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/localisation/LocalisationService.h>
#include <openrct2/platform/Platform.h>

namespace OpenRCT2::UiParityFonts
{
    inline bool IsFamily(const std::string& family)
    {
        return family == "font-ttf-arial-hinted" || family == "font-sprite-fr" || family == "font-ttf-arial-unhinted"
            || family == "font-sprite-ru" || family == "font-ttf-ja" || family == "font-family-fallback-vi";
    }

    inline bool IsTtf(const std::string& family)
    {
        return IsFamily(family) && family != "font-sprite-fr" && family != "font-sprite-ru";
    }

    inline bool IsFallback(const std::string& family)
    {
        return family == "font-family-fallback-vi";
    }

    inline int32_t ExpectedHintingThreshold(const std::string& family)
    {
        if (!IsTtf(family) || family == "font-ttf-arial-unhinted")
            return 0;
        return family == "font-ttf-ja" ? 60 : 40;
    }

    inline size_t MidCaret(const std::string& family)
    {
        if (family == "font-ttf-ja")
            return std::string(u8"観覧車").size();
        return IsTtf(family) ? std::string(u8"AVATAR Café").size() : std::string(u8"Café").size();
    }

    inline const char* ExpectedFontSha256(const std::string& family)
    {
        return family == "font-ttf-ja" ? "4bde3e6392b96910fb59094c6c1a4dbfae18fee78d0bf13dc30616837c4f95db"
                                       : "c9b76220a5be42ead4733611e417cd65c5fd8aeaa33eb56576ac378a37d130a1";
    }

#ifndef DISABLE_TTF
    inline json_t RequiredAbsentFonts(const std::string& family)
    {
        auto result = json_t::array();
        if (IsFallback(family))
        {
            for (const auto* filename : { "OpenRCT2-parity-intentionally-missing.ttf", "arialuni.ttf" })
            {
                TTFFontDescriptor descriptor{};
                descriptor.filename = filename;
                const auto path = Platform::GetFontPath(descriptor);
                UiParityFixtures::Require(
                    !path.empty() && !std::filesystem::exists(std::filesystem::u8path(path)),
                    "Pinned missing font is unexpectedly installed");
                result.push_back({ { "filename", filename }, { "resolvedPath", path }, { "exists", false } });
            }
        }
        return result;
    }
#endif

    inline std::vector<std::string> Steps()
    {
        return { "font-empty-ui",  "font-localized-window", "font-caret-start", "font-caret-mid",
                 "font-caret-end", "font-screen-clip",      "font-restored" };
    }

    inline std::string Text(const std::string& family)
    {
        const std::string phrase = family == "font-ttf-ja" ? u8"観覧車 入口 日本語 あいうえお ✓ → "
            : IsTtf(family)                                ? u8"AVATAR Café Việt Nam Đường vào ✓ → "
                                                           : u8"Café été Straße Œuf — Жук Я ";
        std::string result;
        for (int i = 0; i < 4; i++)
            result += phrase;
        return result;
    }

    inline std::string FileSha256(const std::string& path)
    {
        std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
        UiParityFixtures::Require(stream.is_open(), "Required font/language file cannot be opened");
        auto hasher = Crypt::CreateSHA256();
        std::array<char, 16384> buffer{};
        while (stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || stream.gcount() != 0)
            hasher->Update(buffer.data(), static_cast<size_t>(stream.gcount()));
        UiParityFixtures::Require(stream.eof(), "Required font/language file could not be read completely");
        std::string result;
        constexpr char hex[] = "0123456789abcdef";
        for (auto value : hasher->Finish())
        {
            result += hex[value >> 4];
            result += hex[value & 15];
        }
        return result;
    }

    inline void Configure(const std::string& family)
    {
        // This runs after park load. Each custom profile gets a fresh process:
        // the production custom descriptor captures config pointers once.
        auto& config = Config::Get();
        config.fonts.fileName = IsFallback(family) ? "OpenRCT2-parity-intentionally-missing.ttf"
            : family == "font-ttf-ja"              ? "msgothic.ttc"
            : IsTtf(family)                        ? "arial.ttf"
                                                   : "";
        config.fonts.fontName = IsFallback(family) ? "OpenRCT2 Parity Intentionally Missing"
            : family == "font-ttf-ja"              ? "MS Gothic"
            : IsTtf(family)                        ? "Arial"
                                                   : "";
        config.fonts.sizeTiny = config.fonts.sizeSmall = config.fonts.sizeMedium = 12;
        config.fonts.heightTiny = config.fonts.heightSmall = config.fonts.heightMedium = 14;
        config.fonts.offsetX = 0;
        config.fonts.offsetY = -1;
        config.fonts.enableHinting = family != "font-ttf-arial-unhinted";
        config.fonts.hintingThreshold = family == "font-ttf-ja" ? 60 : 40;
        config.general.language = family == "font-ttf-ja" ? LANGUAGE_JAPANESE
            : family == "font-sprite-ru"                  ? LANGUAGE_RUSSIAN
            : IsTtf(family)                               ? LANGUAGE_VIETNAMESE
                                                          : LANGUAGE_FRENCH;
#ifndef DISABLE_TTF
        (void)RequiredAbsentFonts(family);
#endif
        UiParityFixtures::Require(LanguageOpen(config.general.language), "Required fixture language failed to load");
        auto& localisation = GetContext()->GetLocalisationService();
        UiParityFixtures::Require(
            localisation.GetCurrentLanguage() == config.general.language, "Fixture language silently fell back");
        UiParityFixtures::Require(
            localisation.UseTrueTypeFont() == IsTtf(family), "Actual selected font mode differs from fixture");
#ifdef DISABLE_TTF
        UiParityFixtures::Require(!IsTtf(family), "TTF fixture requires a TTF-enabled executable");
#else
        if (IsTtf(family))
        {
            UiParityFixtures::Require(gCurrentTTFFontSet != nullptr, "Required TrueType font set is absent");
            for (auto style : kFontStyles)
            {
                const auto* font = TTFGetFontFromSpriteBase(style);
                UiParityFixtures::Require(
                    font != nullptr && font->font != nullptr, "Required TrueType font style did not load");
                const bool japanese = family == "font-ttf-ja";
                const bool tinyFallback = IsFallback(family) && style == FontStyle::tiny;
                UiParityFixtures::Require(
                    std::string(font->filename) == (japanese ? "msgothic.ttc" : "arial.ttf")
                        && std::string(font->font_name) == (japanese ? "MS Gothic" : "Arial")
                        && font->ptSize == (tinyFallback ? 10 : 12)
                        && font->line_height == (IsFallback(family) ? (tinyFallback ? 9 : 12) : 14) && font->offset_x == 0
                        && font->offset_y == -1 && font->hinting_threshold == (japanese ? 60 : 40)
                        && TTF_GetFontHinting(font->font) == (config.fonts.enableHinting ? 1 : 0),
                    "Loaded TrueType descriptor differs from pinned profile");
                UiParityFixtures::Require(
                    FileSha256(Platform::GetFontPath(*font)) == ExpectedFontSha256(family),
                    "Installed font differs from pinned font bytes");
            }
        }
#endif
    }

    inline json_t Metadata(const std::string& family)
    {
        auto& localisation = GetContext()->GetLocalisationService();
        auto languages = json_t::array();
        for (auto id : localisation.GetLanguageOrder())
        {
            const auto path = localisation.GetLanguagePath(id);
            languages.push_back({ { "locale", LanguagesDescriptors[id].locale }, { "sha256", FileSha256(path) } });
        }
        auto styles = json_t::array();
        const auto text = Text(family);
        auto cmap = json_t::array();
        std::vector<size_t> boundaries{ 0 };
        const char* cursor = text.c_str();
        while (*cursor != '\0')
        {
            const auto codepoint = UTF8GetNext(cursor, &cursor);
            boundaries.push_back(static_cast<size_t>(cursor - text.c_str()));
            // U+2713 is explicitly sprite-forced by the production text path.
            // U+2192 is a normal TTF arrow; UnicodeChar::right is U+25B6 instead.
            const bool spriteSymbol = codepoint == 0x2713;
            bool provided = true;
#ifndef DISABLE_TTF
            if (IsTtf(family))
                provided = TTFProvidesGlyph(TTFGetFontFromSpriteBase(FontStyle::medium)->font, codepoint);
            else
#endif
            {
                char encoded[5]{};
                UTF8WriteCodepoint(encoded, codepoint);
                provided = FontSupportsStringSprite(encoded);
            }
            UiParityFixtures::Require(provided || (IsTtf(family) && spriteSymbol), "Required text codepoint is unsupported");
            json_t glyph{ { "codepoint", codepoint },
                          { "fontProvidesGlyph", provided },
                          { "intentionalSpriteSymbol", IsTtf(family) && spriteSymbol } };
            if (!IsTtf(family) || spriteSymbol)
            {
                char encoded[5]{};
                UTF8WriteCodepoint(encoded, codepoint);
                UiParityFixtures::Require(FontSupportsStringSprite(encoded), "Required sprite glyph is unavailable");
                glyph["spriteGlyphOffset"] = FontSpriteGetCodepointOffset(static_cast<int32_t>(codepoint));
            }
            cmap.push_back(std::move(glyph));
        }
#ifndef DISABLE_TTF
        if (IsTtf(family))
        {
            for (auto style : kFontStyles)
            {
                const auto* font = TTFGetFontFromSpriteBase(style);
                const auto path = Platform::GetFontPath(*font);
                styles.push_back({ { "style", static_cast<uint8_t>(style) },
                                   { "filename", font->filename },
                                   { "fontName", font->font_name },
                                   { "resolvedPath", path },
                                   { "sha256", FileSha256(path) },
                                   { "faceIndex", 0 },
                                   { "pointSize", font->ptSize },
                                   { "lineHeight", font->line_height },
                                   { "offset", { font->offset_x, font->offset_y } },
                                   { "hintingThreshold", font->hinting_threshold },
                                   { "actualHinting", TTF_GetFontHinting(font->font) } });
            }
        }
#endif
        std::string wrapped;
        int32_t lines{};
        Drawing::wrapString(text, 250 - (24 + 13), FontStyle::medium, &wrapped, &lines);
        json_t result{ { "locale", std::string(localisation.GetCurrentLanguageLocale()) },
                       { "languageFiles", languages },
                       { "trueTypeEnabled", localisation.UseTrueTypeFont() },
                       { "styles", styles },
                       { "text", text },
                       { "textByteLength", text.size() },
                       { "codepointBoundaries", boundaries },
                       { "cmap", cmap },
                       { "textWidth", Drawing::getStringWidth(text, FontStyle::medium, true) },
                       { "wrappedLineBreaks", lines },
                       { "wrappedUtf8Bytes", std::vector<uint8_t>(wrapped.begin(), wrapped.end()) },
                       { "caretUnit", "UTF-8 byte offset at a validated codepoint boundary" },
                       { "shapingScope", "existing FreeType glyph and kerning path; no general shaping or IME claim" } };
        // Preserve the already-qualified families' complete metadata contract.
        if (family != "font-ttf-arial-hinted" && family != "font-sprite-fr")
        {
            const auto& fonts = Config::Get().fonts;
            result["requestedProfile"] = { { "filename", fonts.fileName },
                                           { "fontName", fonts.fontName },
                                           { "enableHinting", fonts.enableHinting },
                                           { "effectiveMediumThreshold", ExpectedHintingThreshold(family) },
                                           { "fallbackExpected", IsFallback(family) } };
#ifndef DISABLE_TTF
            result["requiredAbsentFonts"] = RequiredAbsentFonts(family);
#endif
            if (family == "font-ttf-ja")
                result["pinnedCollectionFace"] = { { "index", 0 },
                                                   { "family", "MS Gothic" },
                                                   { "proof", "pinned TTC bytes and production TTF_OpenFont index 0" } };
        }
        return result;
    }

    template<typename Capture>
    void Run(const std::string& family, Capture&& capture)
    {
        using namespace Ui::Windows;
        auto* manager = Ui::GetWindowManager();
        const auto emit = [&](const char* step, size_t caret = 0, int updates = 0) {
            auto input = UiParityFixtures::WindowInputState();
            input["fontTextInput"] = { { "caret", caret }, { "dialogUpdates", updates } };
            if (const auto* dialog = manager->FindByClass(WindowClass::textinput))
            {
                // The real dialog draws its input below the description and
                // above OK/Cancel. Exclude its title from TTF route admission.
                input["fontTextInput"]["textBounds"] = { dialog->windowPos.x + 12,
                                                         dialog->windowPos.y + dialog->widgets[1].bottom + 38,
                                                         dialog->windowPos.x + dialog->width - 13,
                                                         dialog->windowPos.y + dialog->height - 26 };
            }
            capture(step, input);
        };
        emit("font-empty-ui");
        auto* research = ResearchOpen();
        UiParityFixtures::Require(research != nullptr, "Font fixture Research window did not open");
        WindowSetPosition(*research, { 260, 132 });
        emit("font-localized-window");
        int submitted = 0;
        int cancelled = 0;
        std::string received;
        const auto text = Text(family);
        const auto open = [&](const std::string& value) {
            WindowTextInputOpen(
                "Renderer font parity", "Fixed multilingual wrapping and caret sample.", value, 512,
                [&](std::string_view result) {
                    submitted++;
                    received = result;
                },
                [&]() { cancelled++; });
            auto* dialog = manager->FindByClass(WindowClass::textinput);
            UiParityFixtures::Require(dialog != nullptr, "Font fixture text input did not open");
            dialog->onPrepareDraw();
            return dialog;
        };
        auto* empty = open("");
        const auto emptyHeight = empty->height;
        manager->Close(*empty);
        WindowCullDead();
        // Closing the probe may invoke cancellation. Only submission of the
        // actual sample below belongs to the recorded acceptance contract.
        submitted = cancelled = 0;
        auto* dialog = open(text);
        UiParityFixtures::Require(dialog->height > emptyHeight, "Multilingual text did not wrap into a taller dialog");
        const auto mid = MidCaret(family);
        const auto setCaret = [&](size_t offset) {
            UiParityFixtures::Require(
                offset <= text.size() && (offset == text.size() || UTF8IsCodepointStart(text.c_str() + offset)),
                "Caret is not a UTF-8 boundary");
            SetTextboxCaret(static_cast<int64_t>(offset));
            const auto* session = GetTextboxSession();
            UiParityFixtures::Require(
                session != nullptr && session->Buffer != nullptr && *session->Buffer == text
                    && session->SelectionStart == offset,
                "UTF-8 text or caret offset changed unexpectedly");
            dialog->invalidate();
        };
        setCaret(0);
        for (int i = 0; i < 16; i++)
            dialog->onUpdate();
        emit("font-caret-start", 0, 16);
        setCaret(mid);
        emit("font-caret-mid", mid, 16);
        setCaret(text.size());
        emit("font-caret-end", text.size(), 16);
        WindowSetPosition(*dialog, { -24, 110 });
        emit("font-screen-clip", text.size(), 16);
        WindowTextInputKey(dialog, SDLK_RETURN);
        WindowCullDead();
        UiParityFixtures::Require(
            submitted == 1 && cancelled == 0 && received == text && manager->FindByClass(WindowClass::textinput) == nullptr,
            "Multilingual submission did not preserve exact UTF-8 text");
        manager->Close(*research);
        WindowCullDead();
        emit("font-restored");
    }
} // namespace OpenRCT2::UiParityFonts
