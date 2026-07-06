#include "ArchiveIntegration.h"
#include <juce_core/juce_core.h>

// ── Internal helpers ─────────────────────────────────────────────────────────

juce::ThreadPool& ArchiveIntegration::pool()
{
    static juce::ThreadPool tp { 2 };
    return tp;
}

juce::String ArchiveIntegration::identifierFromUrl (const juce::String& url)
{
    const juce::String prefix = "archive.org/details/";
    int idx = url.indexOf (prefix);
    if (idx >= 0)
    {
        auto id = url.substring (idx + prefix.length())
                     .upToFirstOccurrenceOf ("/", false, false)
                     .upToFirstOccurrenceOf ("?", false, false)
                     .trim();
        return id;
    }

    // Bare identifier — no slashes, no scheme
    auto trimmed = url.trim();
    if (! trimmed.contains ("//") && ! trimmed.contains ("/") && trimmed.isNotEmpty())
        return trimmed;

    return {};
}

bool ArchiveIntegration::isValidArchiveUrl (const juce::String& url)
{
    auto trimmed = url.trim();
    if (trimmed.isEmpty()) return false;

    // Full archive.org URL
    if (trimmed.containsIgnoreCase ("archive.org/details/"))
    {
        auto id = identifierFromUrl (trimmed);
        return id.isNotEmpty();
    }

    // Bare identifier: alphanumerics, hyphens, underscores, dots only
    if (! trimmed.contains ("/") && ! trimmed.contains ("://"))
    {
        for (auto c : trimmed)
            if (! juce::CharacterFunctions::isLetterOrDigit (c) && c != '-' && c != '_' && c != '.')
                return false;
        return trimmed.length() >= 2;
    }

    return false;
}

// ── Cache directory ──────────────────────────────────────────────────────────

juce::File ArchiveIntegration::getCacheDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DYSEKT/archive_cache");
}

juce::File ArchiveIntegration::getTempDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DYSEKT/archive_temp");
}

void ArchiveIntegration::clearTemp()
{
    auto dir = getTempDir();
    if (! dir.isDirectory()) return;

    auto files = dir.findChildFiles (juce::File::findFiles, false);
    for (auto& f : files)
        f.deleteFile();
}

void ArchiveIntegration::downloadTemp (const juce::String& downloadUrl,
                                       std::function<void (bool ok, juce::File tempFile)> cb)
{
    auto filename = downloadUrl.fromLastOccurrenceOf ("/", false, false)
                               .upToFirstOccurrenceOf ("?", false, false)
                               .trim();
    if (filename.isEmpty())
        filename = juce::String (std::abs ((int) downloadUrl.hashCode())) + ".bin";

    auto tempDir   = getTempDir();
    auto tempFile  = tempDir.getChildFile (filename);

    // Reuse existing temp file without re-downloading
    if (tempFile.existsAsFile())
    {
        juce::MessageManager::callAsync ([cb, tempFile] { cb (true, tempFile); });
        return;
    }

    pool().addJob ([downloadUrl, tempFile, cb]
    {
        tempFile.getParentDirectory().createDirectory();

        auto stream = juce::URL (downloadUrl).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (30000)
                .withExtraHeaders ("User-Agent: DYSEKT/1.0"));

        if (stream == nullptr)
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        juce::FileOutputStream out (tempFile);
        if (! out.openedOk())
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        out.writeFromInputStream (*stream, -1);
        out.flush();

        const bool ok = tempFile.existsAsFile() && tempFile.getSize() > 0;
        if (! ok) tempFile.deleteFile();

        juce::MessageManager::callAsync ([cb, ok, tempFile] { cb (ok, tempFile); });
    });
}

juce::int64 ArchiveIntegration::getCacheSize()
{
    auto dir = getCacheDir();
    if (! dir.isDirectory()) return 0;

    juce::int64 total = 0;
    auto files = dir.findChildFiles (juce::File::findFiles, false);
    for (auto& f : files)
        total += f.getSize();
    return total;
}

void ArchiveIntegration::clearCache()
{
    auto dir = getCacheDir();
    if (! dir.isDirectory()) return;

    auto files = dir.findChildFiles (juce::File::findFiles, false);
    for (auto& f : files)
        f.deleteFile();
}

// ── streamPreview ─────────────────────────────────────────────────────────────

void ArchiveIntegration::streamPreview (const juce::String& downloadUrl,
                                         juce::AudioFormatManager& formatManager,
                                         std::function<void (juce::AudioFormatReader*)> cb)
{
    if (downloadUrl.isEmpty())
    {
        juce::MessageManager::callAsync ([cb] { cb (nullptr); });
        return;
    }

    auto* fmPtr = &formatManager;

    pool().addJob ([downloadUrl, fmPtr, cb]
    {
        auto netStream = juce::URL (downloadUrl).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (10000)
                .withExtraHeaders ("User-Agent: DYSEKT/1.0")
                .withNumRedirectsToFollow (3));

        if (netStream == nullptr)
        {
            juce::MessageManager::callAsync ([cb] { cb (nullptr); });
            return;
        }

        // Network streams are non-seekable; AudioFormatReaders require seekability.
        // Buffer into memory first to produce a seekable MemoryInputStream.
        juce::MemoryOutputStream mo;
        mo.writeFromInputStream (*netStream, -1);

        if (mo.getDataSize() == 0)
        {
            juce::MessageManager::callAsync ([cb] { cb (nullptr); });
            return;
        }

        auto memStream = std::make_unique<juce::MemoryInputStream> (mo.getData(),
                                                                     mo.getDataSize(),
                                                                     true /* copy */);
        auto* reader = fmPtr->createReaderFor (std::move (memStream));
        juce::MessageManager::callAsync ([cb, reader] { cb (reader); });
    });
}

// ── fetchItem ────────────────────────────────────────────────────────────────

void ArchiveIntegration::fetchItem (const juce::String& url,
                                    std::function<void (bool ok, Item item)> cb)
{
    auto identifier = identifierFromUrl (url);
    if (identifier.isEmpty())
    {
        juce::MessageManager::callAsync ([cb] { cb (false, {}); });
        return;
    }

    pool().addJob ([identifier, cb]
    {
        // ── 1. Fetch metadata JSON ──────────────────────────────────────────
        juce::URL metaUrl ("https://archive.org/metadata/" + identifier);
        juce::StringPairArray headers;
        auto stream = metaUrl.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (10000)
                .withExtraHeaders ("User-Agent: DYSEKT/1.0"));

        if (stream == nullptr)
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        auto json = stream->readEntireStreamAsString();
        auto root = juce::JSON::parse (json);
        if (! root.isObject())
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        Item item;
        item.identifier = identifier;

        auto meta = root["metadata"];
        if (meta.isObject())
        {
            item.title = meta["title"].toString();
            item.isCollection = (meta["mediatype"].toString() == "collection");
        }
        if (item.title.isEmpty())
            item.title = identifier;

        // ── 2. Parse audio files ────────────────────────────────────────────
        static const juce::StringArray kAudioFormats {
            "wav", "flac", "mp3", "ogg", "aiff", "aif"
        };

        auto files = root["files"];
        if (files.isArray())
        {
            for (int i = 0; i < files.size(); ++i)
            {
                auto f = files[i];
                if (! f.isObject()) continue;

                auto fmt    = f["format"].toString().trim();
                auto name   = f["name"].toString().trim();
                auto ext    = name.fromLastOccurrenceOf (".", false, false).toLowerCase();

                bool isAudio = false;
                for (auto& af : kAudioFormats)
                    if (ext == af || fmt.equalsIgnoreCase (af)) { isAudio = true; break; }
                // Also catch "24bit Lossless", "VBR MP3" etc.
                if (! isAudio && (fmt.containsIgnoreCase ("wav") ||
                                  fmt.containsIgnoreCase ("flac") ||
                                  fmt.containsIgnoreCase ("mp3")  ||
                                  fmt.containsIgnoreCase ("ogg")  ||
                                  fmt.containsIgnoreCase ("aiff")))
                    isAudio = true;

                if (! isAudio) continue;

                AudioFile af;
                af.name        = name;
                af.format      = fmt.isEmpty() ? ext.toUpperCase() : fmt;
                af.downloadUrl = "https://archive.org/download/" + identifier + "/" + name;
                af.sizeBytes   = f["size"].toString().getLargeIntValue();
                item.audioFiles.add (af);
            }
        }

        juce::MessageManager::callAsync ([cb, item] { cb (true, item); });
    });
}

// ── fetchCollection ──────────────────────────────────────────────────────────

void ArchiveIntegration::fetchCollection (const juce::String& collectionId,
                                          std::function<void (bool ok, juce::Array<CollectionEntry> entries)> cb)
{
    pool().addJob ([collectionId, cb]
    {
        // archive.org Scraping API — returns up to 500 items from a collection
        juce::String apiUrl =
            "https://archive.org/advancedsearch.php?q=collection%3A"
            + juce::URL::addEscapeChars (collectionId, true)
            + "&fl[]=identifier&fl[]=title&rows=500&output=json";

        auto stream = juce::URL (apiUrl).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (15000)
                .withExtraHeaders ("User-Agent: DYSEKT/1.0"));

        if (stream == nullptr)
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        auto root = juce::JSON::parse (stream->readEntireStreamAsString());
        juce::Array<CollectionEntry> entries;

        auto response = root["response"];
        if (response.isObject())
        {
            auto docs = response["docs"];
            if (docs.isArray())
            {
                for (int i = 0; i < docs.size(); ++i)
                {
                    auto d = docs[i];
                    if (! d.isObject()) continue;

                    CollectionEntry e;
                    e.identifier = d["identifier"].toString().trim();
                    e.title      = d["title"].toString().trim();
                    if (e.title.isEmpty()) e.title = e.identifier;
                    if (e.identifier.isNotEmpty())
                        entries.add (e);
                }
            }
        }

        const bool ok = entries.size() > 0;
        juce::MessageManager::callAsync ([cb, ok, entries] { cb (ok, entries); });
    });
}

// ── downloadFile ─────────────────────────────────────────────────────────────

void ArchiveIntegration::downloadFile (const juce::String& downloadUrl,
                                       std::function<void (bool ok, juce::File localFile)> cb)
{
    // Derive a stable filename from the URL
    auto filename = downloadUrl.fromLastOccurrenceOf ("/", false, false)
                               .upToFirstOccurrenceOf ("?", false, false)
                               .trim();
    if (filename.isEmpty())
        filename = juce::String (std::abs ((int) downloadUrl.hashCode())) + ".bin";

    auto cacheDir  = getCacheDir();
    auto localFile = cacheDir.getChildFile (filename);

    // Return cached version immediately (still dispatched async for consistency)
    if (localFile.existsAsFile())
    {
        juce::MessageManager::callAsync ([cb, localFile] { cb (true, localFile); });
        return;
    }

    pool().addJob ([downloadUrl, localFile, cb]
    {
        localFile.getParentDirectory().createDirectory();

        auto stream = juce::URL (downloadUrl).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (30000)
                .withExtraHeaders ("User-Agent: DYSEKT/1.0"));

        if (stream == nullptr)
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        juce::FileOutputStream out (localFile);
        if (! out.openedOk())
        {
            juce::MessageManager::callAsync ([cb] { cb (false, {}); });
            return;
        }

        out.writeFromInputStream (*stream, -1);
        out.flush();

        const bool ok = localFile.existsAsFile() && localFile.getSize() > 0;
        if (! ok) localFile.deleteFile();

        juce::MessageManager::callAsync ([cb, ok, localFile] { cb (ok, localFile); });
    });
}
