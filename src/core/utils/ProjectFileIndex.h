#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace NexusAI::ProjectFileIndex {

// One in-progress "@" mention under the text cursor.
struct Mention {
    bool active{false};
    int start{-1};       // index of '@' in the text
    QString query;       // text after '@' (quotes stripped)
    bool quoted{false};  // token began with a quote (path may contain spaces)
};

struct ReferenceLimits {
    int maxFiles{8};
    qint64 maxBytesPerFile{24 * 1024};
    qint64 maxTotalBytes{96 * 1024};
};

struct Reference {
    QString path;       // project-relative, '/' separators
    QString content;
    bool truncated{false};
};

// Cheap per-token validation for the composer (no file contents are read).
// error is empty when the token resolves to a readable project file, or one
// of: "no-project", "missing", "outside", "sensitive".
struct ReferenceCheck {
    QString token;      // normalized token, without '@'
    QString path;       // project-relative path when resolved
    QString error;
};

// Completion / parsing are pure so the QML side and the tests share one
// implementation (no mirrored JS regex).
Mention mentionAt(const QString& text, int cursor);
QStringList mentionPaths(const QString& text);

// Recursively list indexable project files (relative, '/' separators).
// Skips VCS/build/dependency directories, symlinks, sensitive files and
// binary extensions, and stops at maxFiles.
QStringList scan(const QString& root, int maxFiles = 20000);

// Relevance-ordered matches: basename prefix > basename contains > path
// prefix > path contains; case-insensitive.
QStringList complete(const QStringList& files, const QString& query, int limit = 50);

bool isIgnoredDir(const QString& name);
bool isIndexableFile(const QString& name);
bool isSensitiveFile(const QString& name);

// Resolve '@' paths against root for the request context. Rejects absolute
// paths, '..' escapes, symlinks pointing outside root, sensitive/binary
// files and duplicates. Content is byte-capped and marked when truncated.
QVector<Reference> resolveReferences(const QString& root, const QString& text,
                                     const ReferenceLimits& limits = {});

// Validate every '@' token without reading file contents, so the composer can
// show which references will actually travel with the message. Deduplicated
// like resolveReferences and capped at 32 tokens.
QVector<ReferenceCheck> checkReferences(const QString& root, const QString& text);

} // namespace NexusAI::ProjectFileIndex
