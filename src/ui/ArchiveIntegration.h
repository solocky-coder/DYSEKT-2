#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

/**
 * Utility class for Internet Archive (archive.org) integration in DYSEKT.
 *
 * All network operations run on a background ThreadPool and deliver results
 * on the JUCE message thread.  No UI dependencies — include freely from any
 * source file.
 *
 * Supported URL forms
 * ───────────────────
 *   https://archive.org/details/IDENTIFIER          single item
 *   https://archive.org/details/COLLECTION_ID       collection
 *   IDENTIFIER                                       bare identifier
 */
class ArchiveIntegration
{
public:
    // ═══════════════════════════════════════════════════════════════════════
    // Data types
    // ═══════════════════════════════════════════════════════════════════════

    /** One audio file within an archive item. */
    struct AudioFile
    {
        juce::String name;           ///< Original filename on archive.org
        juce::String format;         ///< "MP3", "FLAC", "WAV", etc.
        juce::String downloadUrl;    ///< Full https:// download URL
        juce::int64  sizeBytes = 0;
    };

    /** Metadata for a single archive.org item resolved via the metadata API. */
    struct Item
    {
        juce::String title;
        juce::String identifier;
        juce::Array<AudioFile> audioFiles;
        bool isCollection = false;

        /** Pick the best preview URL from this item's file list.
            Priority: MP3 → OGG → FLAC → WAV → first available.
            Returns an empty string if audioFiles is empty. */
        juce::String bestPreviewUrl() const
        {
            static const char* order[] = { "mp3", "ogg", "flac", "wav", nullptr };
            for (const char** ext = order; *ext; ++ext)
                for (auto& af : audioFiles)
                    if (af.name.endsWithIgnoreCase (juce::String (".") + *ext))
                        return af.downloadUrl;
            return audioFiles.isEmpty() ? juce::String{} : audioFiles[0].downloadUrl;
        }
    };

    /** One entry returned from a collection search. */
    struct CollectionEntry
    {
        juce::String title;
        juce::String identifier;
    };

    // ═══════════════════════════════════════════════════════════════════════
    // Async API
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * Fetch metadata for a single archive.org item.
     * The callback is delivered on the JUCE message thread.
     *
     * @param url  Full archive.org URL or bare identifier.
     * @param cb   Called with (success, Item).  On failure ok==false and Item is empty.
     */
    static void fetchItem (const juce::String& url,
                           std::function<void (bool ok, Item item)> cb);

    /**
     * Fetch up to 500 items from an archive.org collection.
     * The callback is delivered on the JUCE message thread.
     *
     * @param collectionId  Identifier string (NOT a full URL).
     * @param cb            Called with (success, entries).
     */
    static void fetchCollection (const juce::String& collectionId,
                                 std::function<void (bool ok, juce::Array<CollectionEntry> entries)> cb);

    /**
     * Download a remote file to the local cache.
     * If the file is already cached the callback fires immediately (still async).
     * The callback is delivered on the JUCE message thread.
     *
     * @param downloadUrl  Full https:// URL to the file.
     * @param cb           Called with (success, localFile).
     */
    static void downloadFile (const juce::String& downloadUrl,
                              std::function<void (bool ok, juce::File localFile)> cb);

    /**
     * Download a remote file to a TEMPORARY location (not the persistent cache).
     * Use this for preview-only downloads — the file will NOT survive a clearTemp() call.
     * If a temp copy already exists it is reused without re-downloading.
     * The callback is delivered on the JUCE message thread.
     */
    static void downloadTemp (const juce::String& downloadUrl,
                              std::function<void (bool ok, juce::File tempFile)> cb);

    /** Delete all files in the temp preview directory. Safe to call at any time. */
    static void clearTemp();

    /**
     * Open a remote archive.org file as a network stream and create an
     * AudioFormatReader for it — no temp file written to disk.
     * Use for preview-only playback.  The reader is owned by the caller.
     * The callback is delivered on the JUCE message thread.
     *
     * @param downloadUrl   Full https:// URL.
     * @param formatManager Must stay alive until the callback fires.
     * @param cb            Called with the reader (nullptr on failure).
     */
    static void streamPreview (const juce::String& downloadUrl,
                                juce::AudioFormatManager& formatManager,
                                std::function<void (juce::AudioFormatReader*)> cb);

    // ═══════════════════════════════════════════════════════════════════════
    // Helpers
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * Extract just the identifier portion from an archive.org URL or bare string.
     * Returns an empty string if the input does not look like an archive.org URL
     * or identifier.
     */
    static juce::String identifierFromUrl (const juce::String& url);

    /**
     * Return true if the string looks like a valid archive.org item or collection URL.
     */
    static bool isValidArchiveUrl (const juce::String& url);

    // ═══════════════════════════════════════════════════════════════════════
    // Cache management
    // ═══════════════════════════════════════════════════════════════════════

    /** Returns the directory used for caching downloaded files. */
    static juce::File getCacheDir();

    /** Returns the directory used for temporary preview downloads. */
    static juce::File getTempDir();

    /** Returns the total number of bytes currently stored in the cache. */
    static juce::int64 getCacheSize();

    /** Deletes all cached files. Non-destructive if the cache dir doesn't exist. */
    static void clearCache();

private:
    static juce::ThreadPool& pool();

    JUCE_DECLARE_NON_COPYABLE (ArchiveIntegration)
};
