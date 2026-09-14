/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ObjectDownloader.h"

#ifndef DISABLE_HTTP

    #include <openrct2/Context.h>
    #include <openrct2/core/Console.hpp>
    #include <openrct2/core/Json.hpp>
    #include <openrct2/core/String.hpp>
    #include <openrct2/localisation/Formatter.h>
    #include <openrct2/localisation/Formatting.h>
    #include <openrct2/localisation/StringIds.h>
    #include <openrct2/object/ObjectRepository.h>
    #include <openrct2/windows/Intent.h>

namespace OpenRCT2::Ui::Windows
{
    static bool InstallDownloadedObject(const ObjectEntryDescriptor& entry, const std::string& name, const std::string& body)
    {
        // The legacy repository API reads the DAT header before parsing the rest of the file.
        if (body.size() < sizeof(RCTObjectEntry))
            return false;
        auto& repository = GetContext()->GetObjectRepository();
        repository.AddObjectFromFile(ObjectGeneration::dat, name, body.data(), body.size());
        return repository.FindObject(entry) != nullptr;
    }

    ObjectDownloader::ObjectDownloader(BackgroundWorker& worker, RequestFunction request, InstallFunction install)
        : _worker(worker)
        , _request(std::move(request))
        , _install(install ? std::move(install) : InstallDownloadedObject)
    {
    }

    ObjectDownloader::~ObjectDownloader()
    {
        Cancel();
    }

    void ObjectDownloader::Cancel()
    {
        _requestJob.cancel();
        _downloadingObjects = false;
        _nextDownloadQueued = false;
        _downloadStatusInfo = {};
        if (_activeDownloader == this)
            _activeDownloader = nullptr;
    }

    void ObjectDownloader::Begin(const std::vector<ObjectEntryDescriptor>& entries)
    {
        if (_activeDownloader != nullptr)
        {
            auto* previous = _activeDownloader;
            previous->Cancel();
            previous->UpdateStatusBox();
        }
        Cancel();
        UpdateStatusBox();
        _lastDownloadStatusInfo = {};
        _downloadStatusInfo = {};
        _lastDownloadSource.clear();
        _downloadedEntries.clear();
        _entries = entries;
        _currentDownloadIndex = 0;
        _downloadingObjects = true;
        _activeDownloader = this;
        QueueNextDownload();
    }

    bool ObjectDownloader::IsDownloading() const
    {
        return _downloadingObjects;
    }

    std::vector<ObjectEntryDescriptor> ObjectDownloader::GetDownloadedEntries() const
    {
        return _downloadedEntries;
    }

    void ObjectDownloader::Update()
    {
        if (_nextDownloadQueued)
        {
            _nextDownloadQueued = false;
            NextDownload();
        }
        UpdateStatusBox();
    }

    void ObjectDownloader::UpdateStatusBox()
    {
        if (_lastDownloadStatusInfo == _downloadStatusInfo)
            return;
        _lastDownloadStatusInfo = _downloadStatusInfo;
        if (_downloadStatusInfo == DownloadStatusInfo{})
        {
            ContextForceCloseWindowByClass(WindowClass::networkStatus);
            return;
        }

        char message[256]{};
        auto ft = Formatter();
        if (_downloadStatusInfo.Source.empty())
        {
            ft.Add<int16_t>(static_cast<int16_t>(_downloadStatusInfo.Count));
            ft.Add<int16_t>(static_cast<int16_t>(_downloadStatusInfo.Total));
            ft.Add<char*>(_downloadStatusInfo.Name.c_str());
            FormatStringLegacy(message, sizeof(message), STR_DOWNLOADING_OBJECTS, ft.Data());
        }
        else
        {
            ft.Add<char*>(_downloadStatusInfo.Name.c_str());
            ft.Add<char*>(_downloadStatusInfo.Source.c_str());
            ft.Add<int16_t>(static_cast<int16_t>(_downloadStatusInfo.Count));
            ft.Add<int16_t>(static_cast<int16_t>(_downloadStatusInfo.Total));
            FormatStringLegacy(message, sizeof(message), STR_DOWNLOADING_OBJECTS_FROM, ft.Data());
        }
        auto intent = Intent(WindowClass::networkStatus);
        intent.PutExtra(INTENT_EXTRA_MESSAGE, std::string(message));
        intent.PutExtra(INTENT_EXTRA_CALLBACK, []() -> void {
            if (_activeDownloader != nullptr)
                _activeDownloader->Cancel();
        });
        ContextOpenIntent(&intent);
    }

    void ObjectDownloader::QueueNextDownload()
    {
        if (_downloadingObjects)
            _nextDownloadQueued = true;
    }

    void ObjectDownloader::StartRequest(Http::Request request, std::function<void(Http::Response)> completion)
    {
        // No controller, context or repository pointer is captured by background work.
        _requestJob = _worker.addJob(
            [request = std::move(request), execute = _request]() {
                try
                {
                    return execute(request);
                }
                catch (const std::exception& ex)
                {
                    Http::Response response;
                    response.status = Http::Status::error;
                    response.error = ex.what();
                    return response;
                }
            },
            [this, completion = std::move(completion)](Http::Response response) {
                if (!_downloadingObjects)
                    return;
                try
                {
                    completion(std::move(response));
                }
                catch (const std::exception& ex)
                {
                    Console::Error::WriteLine("Object download failed: %s", ex.what());
                    QueueNextDownload();
                }
            });
        if (!_requestJob.isValid())
            Cancel();
    }

    void ObjectDownloader::DownloadObject(const ObjectEntryDescriptor& entry, const std::string& name, const std::string& url)
    {
        Http::Request request;
        request.url = url;
        StartRequest(std::move(request), [this, entry, name](Http::Response response) {
            if (response.status == Http::Status::ok && _install(entry, name, response.body))
                _downloadedEntries.push_back(entry);
            else
                Console::Error::WriteLine("  Failed to download %s", name.c_str());
            QueueNextDownload();
        });
    }

    void ObjectDownloader::NextDownload()
    {
        if (!_downloadingObjects || _currentDownloadIndex >= _entries.size())
        {
            Cancel();
            return;
        }

        const auto entry = _entries[_currentDownloadIndex++];
        const auto name = String::trim(std::string(entry.GetName()));
        _downloadStatusInfo = { name, _lastDownloadSource, _currentDownloadIndex, _entries.size() };
        try
        {
            Http::Request request;
            request.url = "https://api.openrct2.io/objects/legacy/" + name;
            StartRequest(std::move(request), [this, entry, name](Http::Response response) {
                if (response.status == Http::Status::ok)
                {
                    auto metadata = Json::FromString(response.body);
                    if (metadata.is_object())
                    {
                        const auto objectName = Json::GetString(metadata["name"]);
                        const auto source = Json::GetString(metadata["source"]);
                        const auto link = Json::GetString(metadata["download"]);
                        if (!link.empty())
                        {
                            _lastDownloadSource = source;
                            _downloadStatusInfo = { name, source, _currentDownloadIndex, _entries.size() };
                            DownloadObject(entry, objectName, link);
                            return;
                        }
                    }
                    Console::Error::WriteLine("  %s query returned no download link", name.c_str());
                }
                else
                {
                    Console::Error::WriteLine(
                        "  %s query failed (status %d)", name.c_str(), static_cast<int32_t>(response.status));
                }
                QueueNextDownload();
            });
        }
        catch (const std::exception& ex)
        {
            Console::Error::WriteLine("  Failed to query %s: %s", name.c_str(), ex.what());
            QueueNextDownload();
        }
    }
} // namespace OpenRCT2::Ui::Windows

#endif
