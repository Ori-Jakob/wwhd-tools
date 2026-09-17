#include "app/cemu.h"
#include "app/hooks.h"
#include "app/present.h"

#include "core/config.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/version.h"
#include "ui/notifications.h"
#include "ui/overlay.h"

#include <coreinit/debug.h>
#include <coreinit/dynload.h>

#include "libwupatch/wupatch.h"
#include "libwwhd/libwwhd.h"

namespace App {
static const RplHost* gHost = nullptr;

struct RplTitleBlob {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
    uint64_t ids[4];
};

static const RplTitleBlob kTitleBlob
    __attribute__((section(".rpltitles"), used, aligned(8))) = {
    RPL_TITLES_MAGIC, 1u, 4u, 0u,
    {
        0x0005000010143500ull,
        0x0005000010143600ull,
        0x0005000010143400ull,
        0x0005000010143599ull,
    },
};

#define kTitles      (kTitleBlob.ids)
#define kTitleCount  (sizeof(kTitleBlob.ids) / sizeof(kTitleBlob.ids[0]))

static uint64_t s_titleIds[kTitleCount];

static void patchLogSink(int level, const char* line)
{
    if (!line)
        return;
    if (level == WuPatch::LOG_ERROR)
        Logger::LogError("%s", line);
    else if (level == WuPatch::LOG_WARN)
        Logger::LogWarn("%s", line);
    else
        Logger::Log("%s", line);
}

static void configurePatchData(const RplHost* host)
{
    for (unsigned i = 0; i < kTitleCount; ++i)
        s_titleIds[i] = kTitles[i];

    WuPatch::Config cfg = {};
    cfg.titleIds       = s_titleIds;
    cfg.titleIdCount   = sizeof(s_titleIds) / sizeof(s_titleIds[0]);
    cfg.executableName = ".rpx";
    cfg.versionMin     = 0;
    cfg.versionMax     = 0xFFFF;
    cfg.tag            = "[wwhd_tools_rpl][patch]";
    cfg.textDelta      = host->textDelta(host);
    cfg.textResolved   = App::g_underCemu;
    cfg.dataDelta      = host->dataDelta(host);
    cfg.dataResolved   = true;
    cfg.backend        = WuPatch::BACKEND_DIRECT;
    cfg.allowDirectWrite = App::g_underCemu;
    cfg.assumeMapped   = App::g_underCemu;
    WuPatch::SetLogSink(patchLogSink);
    WuPatch::Configure(cfg);
    WuPatch::OnApplicationStart();
}

static int OnInit(const RplHost* host)
{
    gHost = host;

    wwhd_textDelta    = host->textDelta(host);
    wwhd_textResolved = 1;
    wwhd_dataDelta    = host->dataDelta(host);
    wwhd_dataResolved = 1;
    wwhd_titleId      = host->titleId(host);

    if (wwhd_selectRegion(u32(wwhd_titleId & 0xFFFFFFFFull)) == WWHD_REGION_NONE) {
        host->log(host, RPL_LOG_ERROR, "region probe failed at text delta %08X",
                  (unsigned)wwhd_textDelta);
        return -1;
    }

    Logger::SetHost(host);
    Logger::Log("wwhd-tools %s by %s, built %s %s, initialised in %s",
                WWHD_TOOLS_VERSION, WWHD_TOOLS_AUTHOR, __DATE__, __TIME__,
                wwhd_regionName());

    configurePatchData(host);

    const bool dynHooked = RegisterDynamicHooks(host);
    Input::SetHost(host);
    Present::Init();
    Ui::Overlay::OnApplicationStart();

    host->log(host, RPL_LOG_INFO, "region %s, %u static hook(s), dynamic hooks %s",
              wwhd_regionName(), (unsigned)gHookCount,
              dynHooked ? "ALL APPLIED" : "SOME FAILED");
    return 0;
}

static void OnDeinit()
{
    Present::Shutdown();
    Ui::Overlay::OnApplicationEnd();
    Input::OnApplicationEnd();
    Notifications::Clear();
    gHost = nullptr;
}

static RplManifest gManifest = {
    .magic        = RPL_MAGIC,
    .abiVersion   = RPL_ABI_VERSION,
    .name         = "wwhd_tools",
    .version      = WWHD_TOOLS_VERSION,
    .author       = WWHD_TOOLS_AUTHOR,
    .titleIds     = kTitles,
    .titleIdCount = kTitleCount,
    .hooks        = nullptr,
    .hookCount    = 0,
    .priority     = 0,
    .flags        = RPL_FLAG_ALLOW_RELEASE,
    .onInit       = OnInit,
    .onDeinit     = OnDeinit,
    .maxHooks     = 24,
    .onReleaseForeground  = Present::OnReleaseForeground,
    .onAcquiredForeground = Present::OnAcquiredForeground,
    .onPadSampled         = Ui::Overlay::OnPadSampled,
};
}

RPL_EXPORT const RplManifest* rpl_manifest()
{
    App::gManifest.hooks     = App::gHooks;
    App::gManifest.hookCount = App::gHookCount;
    return &App::gManifest;
}

RPL_EXPORT int rpl_entry(OSDynLoad_Module module, OSDynLoad_EntryReason reason)
{
    (void)module;
    OSReport("[wwhd_tools_rpl] rpl_entry(reason %d)\n", (int)reason);
    return 0;
}
