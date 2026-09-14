/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ObjectDownloader.h"

#include <openrct2/Context.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/Diagnostic.h>
#include <openrct2/drawing/ColourMap.h>
#include <openrct2/drawing/Rectangle.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/localisation/Formatter.h>
#include <openrct2/localisation/Formatting.h>
#include <openrct2/localisation/StringIds.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/platform/Platform.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/ui/WindowManager.h>
#include <openrct2/windows/Intent.h>
#include <sstream>
#include <string>
#include <vector>

using namespace OpenRCT2::Drawing;

namespace OpenRCT2::Ui::Windows
{
    enum WindowObjectLoadErrorWidgetIdx
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,
        WIDX_COLUMN_OBJECT_NAME,
        WIDX_COLUMN_OBJECT_SOURCE,
        WIDX_COLUMN_OBJECT_TYPE,
        WIDX_SCROLL,
        WIDX_COPY_CURRENT,
        WIDX_COPY_ALL,
        WIDX_DOWNLOAD_ALL
    };

    static constexpr StringId kWindowTitle = STR_OBJECT_LOAD_ERROR_TITLE;
    static constexpr ScreenSize kWindowSize = { 450, 400 };
    static constexpr int32_t kWindowWidthLessPadding = kWindowSize.width - 5;
    constexpr int32_t kNameColLeft = 4;
    constexpr int32_t kSourceColLeft = (kWindowWidthLessPadding / 4) + 1;
    constexpr int32_t kTypeColLeft = 5 * kWindowWidthLessPadding / 8 + 1;

    // clang-format off
    static constexpr auto window_object_load_error_widgets = makeWidgets(
        makeWindowShim(kWindowTitle, kWindowSize),
        makeWidget({  kNameColLeft,  57}, {108,  14}, WidgetType::tableHeader, WindowColour::primary, STR_OBJECT_NAME                         ), // 'Object name' header
        makeWidget({kSourceColLeft,  57}, {166,  14}, WidgetType::tableHeader, WindowColour::primary, STR_OBJECT_SOURCE                       ), // 'Object source' header
        makeWidget({  kTypeColLeft,  57}, {166,  14}, WidgetType::tableHeader, WindowColour::primary, STR_OBJECT_TYPE                         ), // 'Object type' header
        makeWidget({  kNameColLeft,  70}, {442, 298}, WidgetType::scroll,      WindowColour::primary, SCROLL_VERTICAL                         ), // Scrollable list area
        makeWidget({  kNameColLeft, 377}, {145,  14}, WidgetType::button,      WindowColour::primary, STR_COPY_SELECTED, STR_COPY_SELECTED_TIP), // Copy selected button
        makeWidget({           152, 377}, {145,  14}, WidgetType::button,      WindowColour::primary, STR_COPY_ALL,      STR_COPY_ALL_TIP     )  // Copy all button
    #ifndef DISABLE_HTTP
      , makeWidget({           300, 377}, {146,  14}, WidgetType::button,      WindowColour::primary, STR_DOWNLOAD_ALL,  STR_DOWNLOAD_ALL_TIP )  // Download all button
    #endif
    );
    // clang-format on

    /**
     *  Returns an StringId that represents an RCTObjectEntry's type.
     *
     *  Could possibly be moved out of the window file if other
     *  uses exist and a suitable location is found.
     */
    static constexpr StringId GetStringFromObjectType(const ObjectType type)
    {
        switch (type)
        {
            case ObjectType::ride:
                return STR_OBJECT_SELECTION_RIDE_VEHICLES_ATTRACTIONS;
            case ObjectType::smallScenery:
                return STR_OBJECT_SELECTION_SMALL_SCENERY;
            case ObjectType::largeScenery:
                return STR_OBJECT_SELECTION_LARGE_SCENERY;
            case ObjectType::walls:
                return STR_OBJECT_SELECTION_WALLS_FENCES;
            case ObjectType::banners:
                return STR_OBJECT_SELECTION_PATH_SIGNS;
            case ObjectType::paths:
                return STR_OBJECT_SELECTION_FOOTPATHS;
            case ObjectType::pathAdditions:
                return STR_OBJECT_SELECTION_PATH_EXTRAS;
            case ObjectType::sceneryGroup:
                return STR_OBJECT_SELECTION_SCENERY_GROUPS;
            case ObjectType::parkEntrance:
                return STR_OBJECT_SELECTION_PARK_ENTRANCE;
            case ObjectType::water:
                return STR_OBJECT_SELECTION_WATER;
            case ObjectType::terrainSurface:
                return STR_OBJECT_SELECTION_TERRAIN_SURFACES;
            case ObjectType::terrainEdge:
                return STR_OBJECT_SELECTION_TERRAIN_EDGES;
            case ObjectType::station:
                return STR_OBJECT_SELECTION_STATIONS;
            case ObjectType::music:
                return STR_OBJECT_SELECTION_MUSIC;
            case ObjectType::footpathSurface:
                return STR_OBJECT_SELECTION_FOOTPATH_SURFACES;
            case ObjectType::footpathRailings:
                return STR_OBJECT_SELECTION_FOOTPATH_RAILINGS;
            case ObjectType::peepNames:
                return STR_OBJECT_SELECTION_PEEP_NAMES;
            case ObjectType::peepAnimations:
                return STR_OBJECT_SELECTION_PEEP_ANIMATIONS;
            case ObjectType::climate:
                return STR_OBJECT_SELECTION_CLIMATE;
            // Intransient objects, should never pop up here
            case ObjectType::scenarioMeta:
                return STR_OBJECT_SELECTION_SCENARIO_TEXTS;
            case ObjectType::audio:
            default:
                return STR_UNKNOWN_OBJECT_TYPE;
        }
    }

    class ObjectLoadErrorWindow final : public Window
    {
    private:
        std::vector<ObjectEntryDescriptor> _invalidEntries;
        int32_t _highlightedIndex = -1;
        std::string _filePath;
#ifndef DISABLE_HTTP
        ObjectDownloader _objDownloader{ GetContext()->GetBackgroundWorker() };
        bool _updatedListAfterDownload{};

        void DownloadAllObjects()
        {
            if (!_objDownloader.IsDownloading())
            {
                _updatedListAfterDownload = false;
                _objDownloader.Begin(_invalidEntries);
            }
        }

        void UpdateObjectList()
        {
            const auto entries = _objDownloader.GetDownloadedEntries();
            for (auto& de : entries)
            {
                _invalidEntries.erase(
                    std::remove_if(
                        _invalidEntries.begin(), _invalidEntries.end(),
                        [de](const ObjectEntryDescriptor& e) { return de.GetName() == e.GetName(); }),
                    _invalidEntries.end());
            }
            numListItems = static_cast<uint16_t>(_invalidEntries.size());
        }
#endif

        /**
         *  Returns a newline-separated string listing all object names.
         *  Used for placing all names on the clipboard.
         */
        void CopyObjectNamesToClipboard()
        {
            std::stringstream stream;
            for (uint16_t i = 0; i < numListItems; i++)
            {
                const auto& entry = _invalidEntries[i];
                stream << entry.GetName();
                stream << PLATFORM_NEWLINE;
            }

            const auto clip = stream.str();
            GetContext()->GetUiContext().SetClipboardText(clip.c_str());
        }

        void SelectObjectFromList(const int32_t index)
        {
            if (index < 0 || index > numListItems)
            {
                selectedListItem = -1;
            }
            else
            {
                selectedListItem = index;
            }
            invalidateWidget(WIDX_SCROLL);
        }

    public:
        void onOpen() override
        {
            setWidgets(window_object_load_error_widgets);

            WindowInitScrollWidgets(*this);
            colours[0] = Drawing::Colour::lightBlue;
            colours[1] = Drawing::Colour::lightBlue;
            colours[2] = Drawing::Colour::lightBlue;
        }

        void onClose() override
        {
#ifndef DISABLE_HTTP
            const bool wasDownloading = _objDownloader.IsDownloading();
            _objDownloader.Cancel();
            if (wasDownloading)
                ContextForceCloseWindowByClass(WindowClass::networkStatus);
#endif
            _invalidEntries.clear();
            _invalidEntries.shrink_to_fit();
        }

        void onMouseUp(const WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    close();
                    return;
                case WIDX_COPY_CURRENT:
                    if (selectedListItem > -1 && selectedListItem < numListItems)
                    {
                        const auto name = std::string(_invalidEntries[selectedListItem].GetName());
                        GetContext()->GetUiContext().SetClipboardText(name.c_str());
                    }
                    break;
                case WIDX_COPY_ALL:
                    CopyObjectNamesToClipboard();
                    break;
#ifndef DISABLE_HTTP
                case WIDX_DOWNLOAD_ALL:
                    DownloadAllObjects();
                    break;
#endif
            }
        }

        void onUpdate() override
        {
            currentFrame++;

            // Check if the mouse is hovering over the list
            if (!widgetIsHighlighted(*this, WIDX_SCROLL))
            {
                _highlightedIndex = -1;
                invalidateWidget(WIDX_SCROLL);
            }

#ifndef DISABLE_HTTP
            _objDownloader.Update();

            // Remove downloaded objects from our invalid entry list
            if (_objDownloader.IsDownloading())
            {
                // Don't do this too often as it isn't particularly efficient
                if (currentFrame % 64 == 0)
                {
                    UpdateObjectList();
                }
            }
            else if (!_updatedListAfterDownload)
            {
                UpdateObjectList();
                _updatedListAfterDownload = true;
            }
#endif
        }

        ScreenSize onScrollGetSize(const int32_t scrollIndex) override
        {
            return ScreenSize(0, numListItems * kScrollableRowHeight);
        }

        void onScrollMouseDown(const int32_t scrollIndex, const ScreenCoordsXY& screenCoords) override
        {
            const auto selectedItem = screenCoords.y / kScrollableRowHeight;
            SelectObjectFromList(selectedItem);
        }

        void onScrollMouseOver(const int32_t scrollIndex, const ScreenCoordsXY& screenCoords) override
        {
            // Highlight item that the cursor is over, or remove highlighting if none
            const auto selectedItem = screenCoords.y / kScrollableRowHeight;
            if (selectedItem < 0 || selectedItem >= numListItems)
                _highlightedIndex = -1;
            else
                _highlightedIndex = selectedItem;

            invalidateWidget(WIDX_SCROLL);
        }

        void onDraw(RenderTarget& rt) override
        {
            WindowDrawWidgets(*this, rt);

            auto screenPos = windowPos + ScreenCoordsXY{ 5, widgets[WIDX_TITLE].bottom };

            // Draw explanatory message
            auto ft = Formatter();
            ft.Add<StringId>(STR_OBJECT_ERROR_WINDOW_EXPLANATION);
            drawTextWrapped(rt, screenPos + ScreenCoordsXY{ 0, 4 }, kWindowSize.width - 10, STR_BLACK_STRING, ft);

            // Draw file name
            ft = Formatter();
            ft.Add<StringId>(STR_OBJECT_ERROR_WINDOW_FILE);
            ft.Add<utf8*>(_filePath.c_str());
            drawTextEllipsised(rt, screenPos + ScreenCoordsXY{ 0, 29 }, kWindowSize.width - 5, STR_BLACK_STRING, ft);
        }

        void onScrollDraw(const int32_t scrollIndex, RenderTarget& rt) override
        {
            auto rtCoords = ScreenCoordsXY{ rt.x, rt.y };
            Rectangle::fill(
                rt, { rtCoords, rtCoords + ScreenCoordsXY{ rt.width - 1, rt.height - 1 } },
                getColourMap(colours[1].colour).midLight);
            const int32_t listWidth = widgets[WIDX_SCROLL].width() - 1;

            for (int32_t i = 0; i < numListItems; i++)
            {
                ScreenCoordsXY screenCoords;
                screenCoords.y = i * kScrollableRowHeight;
                if (screenCoords.y > rt.y + rt.height)
                    break;

                if (screenCoords.y + kScrollableRowHeight < rt.y)
                    continue;

                const auto screenRect = ScreenRect{ { 0, screenCoords.y },
                                                    { listWidth, screenCoords.y + kScrollableRowHeight - 1 } };
                // If hovering over item, change the color and fill the backdrop.
                if (i == selectedListItem)
                    Rectangle::fill(rt, screenRect, getColourMap(colours[1].colour).darker);
                else if (i == _highlightedIndex)
                    Rectangle::fill(rt, screenRect, getColourMap(colours[1].colour).midDark);
                else if ((i & 1) != 0) // odd / even check
                    Rectangle::fill(rt, screenRect, getColourMap(colours[1].colour).light);

                // Draw the actual object entry's name...
                screenCoords.x = kNameColLeft - 3;

                const auto& entry = _invalidEntries[i];

                drawText(rt, screenCoords, entry.GetName(), { Colour::darkGreen });

                if (entry.Generation == ObjectGeneration::dat)
                {
                    // ... source game ...
                    const auto sourceStringId = ObjectManagerGetSourceGameString(entry.Entry.GetSourceGame());
                    drawText(rt, { kSourceColLeft - 3, screenCoords.y }, sourceStringId, { Drawing::Colour::darkGreen });
                }

                // ... and type
                const auto type = GetStringFromObjectType(entry.GetType());
                drawText(rt, { kTypeColLeft - 3, screenCoords.y }, type, { Drawing::Colour::darkGreen });
            }
        }

        void initialise(utf8* path, const size_t numMissingObjects, const ObjectEntryDescriptor* missingObjects)
        {
#ifndef DISABLE_HTTP
            const bool wasDownloading = _objDownloader.IsDownloading();
            _objDownloader.Cancel();
            _updatedListAfterDownload = true;
            if (wasDownloading)
                ContextForceCloseWindowByClass(WindowClass::networkStatus);
#endif
            _invalidEntries = std::vector<ObjectEntryDescriptor>(missingObjects, missingObjects + numMissingObjects);

            // Refresh list items and path
            numListItems = static_cast<uint16_t>(numMissingObjects);
            _filePath = path;

            invalidate();
        }
    };

    WindowBase* ObjectLoadErrorOpen(utf8* path, size_t numMissingObjects, const ObjectEntryDescriptor* missingObjects)
    {
        // Check if window is already open
        auto* windowMgr = GetWindowManager();
        auto* window = windowMgr->BringToFrontByClass(WindowClass::objectLoadError);
        if (window == nullptr)
        {
            // The ‘stick to front’ flag is needed because ‘Load game’ also has it set, and not setting it would cause our
            // window to get displayed _under_ ‘Load game’.
            window = windowMgr->Create<ObjectLoadErrorWindow>(
                WindowClass::objectLoadError, kWindowSize, { WindowFlag::stickToFront });
        }

        static_cast<ObjectLoadErrorWindow*>(window)->initialise(path, numMissingObjects, missingObjects);

        return window;
    }
} // namespace OpenRCT2::Ui::Windows
