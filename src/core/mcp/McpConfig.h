#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace NexusAI {

// Batch 31a: mcp_servers.json — one entry per configured MCP server.
// Field-level shape mirrors McpServerConfig (McpClient.h) but stays a
// QVariantMap so the settings page can edit it without core<->viewmodels
// type coupling.
namespace McpConfig {

// Reads <appDataLocation>/mcp_servers.json; missing or malformed file yields
// an empty list (never a hard failure — a broken config must not stop the
// app from starting).
QVector<QVariantMap> load();
// Atomically persists the list; returns false if directory creation, write,
// or commit fails.
bool save(const QVariantList& servers);

QString configFilePath();

} // namespace McpConfig

} // namespace NexusAI
