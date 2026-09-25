// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "MoneyPresentation.h"

#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../config/Config.h"
#include "../entity/EntityTweener.h"
#include "../entity/MoneyEffect.h"
#include "../localisation/Currency.h"
#include "../localisation/Formatting.h"
#include "../localisation/LocalisationService.h"
#include "Colour.h"
#include "ScrollingText.h"

#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace OpenRCT2::Drawing
{
    std::shared_ptr<const MoneyPresentationSnapshot> CaptureMoneyPresentationSnapshot(uint32_t sourceTick)
    {
        auto& state = getGameState();
        if (sourceTick != state.currentTicks)
            throw std::invalid_argument("Money publication requires current owner tick");
        struct Cached
        {
            uint32_t generation{};
            money64 value{};
            uint8_t guestPurchase{};
            std::shared_ptr<const TextGlyphRun> run;
        };
        static std::unordered_map<uint32_t, Cached> cache;
        static std::shared_ptr<const MoneyPresentationSnapshot> held;
        static CurrencyDescriptor currency{};
        static uint64_t assets{}, epoch{}, revision{};
        const auto& config = Config::Get().general;
        const auto& currentCurrency = CurrencyDescriptors[EnumValue(config.currencyFormat)];
        const auto currentAssets = ScrollingText::getAssetRevision();
        const auto currentEpoch = state.entities.GetEntityVisualEpoch();
        const bool reset = assets != currentAssets || epoch != currentEpoch || currency.rate != currentCurrency.rate
            || currency.affix_unicode != currentCurrency.affix_unicode || currency.affix_ascii != currentCurrency.affix_ascii
            || std::memcmp(currency.symbol_unicode, currentCurrency.symbol_unicode, sizeof(currency.symbol_unicode)) != 0
            || std::memcmp(currency.symbol_ascii, currentCurrency.symbol_ascii, sizeof(currency.symbol_ascii)) != 0;
        if (reset)
            cache.clear();
        auto result = std::make_shared<MoneyPresentationSnapshot>();
        result->epoch = currentEpoch;
        result->sourceTick = sourceTick;
        auto records = std::make_shared<std::vector<MoneyPresentationRecord>>();
        auto catalog = std::make_shared<MoneyGlyphCatalog>();
        std::unordered_map<uint32_t, Cached> nextCache;
        if (!gOpenRCT2NoGraphics && gLegacyScene != LegacyScene::titleSequence)
        {
            const bool forceSprite = LocalisationService_UseTrueTypeFont()
                && FontSupportsStringSprite(currentCurrency.symbol_unicode);
            const auto& list = state.entities.GetEntityExecutionList(EntityType::moneyEffect);
            records->reserve(list.size());
            nextCache.reserve(list.size());
            for (const auto* entity : list)
            {
                const auto& money = *static_cast<const MoneyEffect*>(entity);
                if (money.guestPurchase && !config.showGuestPurchases)
                    continue;
                const auto handle = state.entities.GetEntityVisualHandle(money.id);
                const auto id = money.id.ToUnderlying();
                auto found = cache.find(id);
                Cached entry;
                if (found != cache.end() && found->second.generation == handle.generation && found->second.value == money.value
                    && found->second.guestPurchase == money.guestPurchase)
                    entry = found->second;
                else
                {
                    auto [stringId, value] = money.getStringId();
                    char formatted[256]{};
                    FormatStringLegacy(formatted, sizeof(formatted), stringId, &value);
                    entry = { handle.generation, money.value, money.guestPurchase,
                              std::make_shared<const TextGlyphRun>(
                                  CompileTextGlyphRun(formatted, { Colour::black }, FontStyle::medium, forceSprite)) };
                }
                auto position = money.getLocation();
                if (const auto motion = EntityTweener::get().GetMotion(money))
                    position = motion->current;
                records->push_back({ position.x, position.y, position.z, id, handle.generation, money.wiggle, money.offsetX,
                                     static_cast<uint32_t>(catalog->runs.size()), money.guestPurchase });
                catalog->runs.push_back(entry.run);
                nextCache.emplace(id, std::move(entry));
            }
        }
        const bool compatible = held && held->epoch == currentEpoch;
        result->records = compatible && *held->records == *records ? held->records : records;
        result->catalog = compatible && held->catalog->runs == catalog->runs ? held->catalog : catalog;
        result->revision = compatible && result->records == held->records && result->catalog == held->catalog ? held->revision
                                                                                                              : ++revision;
        cache = std::move(nextCache);
        currency = currentCurrency;
        assets = currentAssets;
        epoch = currentEpoch;
        held = result;
        return result;
    }
} // namespace OpenRCT2::Drawing
