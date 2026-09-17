#pragma once

struct ImGuiIO;

namespace Notifications {
enum Kind {
    Info = 0,
    Success = 1,
    Warning = 2,
    Error = 3,
};

void Show(const char* message);
void Show(Kind kind, const char* message);
void Show(Kind kind, const char* title, const char* message);
void ShowKeyed(const char* key, Kind kind, const char* message);
void ShowKeyed(const char* key, Kind kind, const char* title, const char* message);

void Showf(const char* format, ...);
void Showf(Kind kind, const char* format, ...);
void ShowTitledf(Kind kind, const char* title, const char* format, ...);
void ShowKeyedf(const char* key, Kind kind, const char* format, ...);
void ShowKeyedTitledf(const char* key, Kind kind, const char* title, const char* format, ...);

void ShowSticky(const char* key, Kind kind, const char* title, const char* message);
void ShowStickyf(const char* key, Kind kind, const char* title, const char* format, ...);
void Dismiss(const char* key);

void Draw(ImGuiIO& io);
void Clear();
}
