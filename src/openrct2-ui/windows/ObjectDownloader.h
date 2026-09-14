/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifndef DISABLE_HTTP

    #include <openrct2/core/BackgroundWorker.hpp>
    #include <openrct2/core/Http.h>
    #include <openrct2/object/Object.h>

namespace OpenRCT2::Ui::Windows
{
    // Controller state, completion callbacks and repository writes belong to the main thread.
    // Only the value-captured HTTP request runs on the context's owned background worker.
    class ObjectDownloader
    {
    public:
        using RequestFunction = std::function<Http::Response(const Http::Request&)>;
        using InstallFunction = std::function<bool(const ObjectEntryDescriptor&, const std::string&, const std::string&)>;

        explicit ObjectDownloader(BackgroundWorker& worker, RequestFunction request = Http::Do, InstallFunction install = {});
        ~ObjectDownloader();
        ObjectDownloader(const ObjectDownloader&) = delete;
        ObjectDownloader& operator=(const ObjectDownloader&) = delete;

        void Begin(const std::vector<ObjectEntryDescriptor>& entries);
        void Cancel();
        bool IsDownloading() const;
        std::vector<ObjectEntryDescriptor> GetDownloadedEntries() const;
        void Update();

    private:
        struct DownloadStatusInfo
        {
            std::string Name;
            std::string Source;
            size_t Count{};
            size_t Total{};
            bool operator==(const DownloadStatusInfo&) const = default;
        };

        BackgroundWorker& _worker;
        RequestFunction _request;
        InstallFunction _install;
        BackgroundWorker::Job _requestJob;
        std::vector<ObjectEntryDescriptor> _entries;
        std::vector<ObjectEntryDescriptor> _downloadedEntries;
        size_t _currentDownloadIndex{};
        bool _nextDownloadQueued{};
        bool _downloadingObjects{};
        DownloadStatusInfo _lastDownloadStatusInfo;
        DownloadStatusInfo _downloadStatusInfo;
        std::string _lastDownloadSource;

        // The existing status-window intent accepts a plain function pointer, not a capturing callback.
        inline static ObjectDownloader* _activeDownloader{};

        void UpdateStatusBox();
        void QueueNextDownload();
        void NextDownload();
        void DownloadObject(const ObjectEntryDescriptor& entry, const std::string& name, const std::string& url);
        void StartRequest(Http::Request request, std::function<void(Http::Response)> completion);
    };
} // namespace OpenRCT2::Ui::Windows

#endif
