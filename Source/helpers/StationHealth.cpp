#include "StationHealth.h"

namespace StationHealth {

Level diskLevel(juce::int64 freeBytes, juce::int64 totalBytes) {
    if (freeBytes < 0) return Level::Ok;
    if (freeBytes < criticalFreeBytes) return Level::Critical;
    if (freeBytes < warningFreeBytes || (totalBytes > 0 && freeBytes * 20 < totalBytes)) return Level::Warning;
    return Level::Ok;
}

Alert evaluate(const Inputs& in) {
    Alert alert;
    auto add = [&alert](Level level, const juce::String& text, const juce::String& detail) {
        if (level > alert.level) {
            alert.level = level;
            alert.text = text;
        }
        alert.details.add(detail);
    };

    const auto disk = diskLevel(in.diskFree, in.diskTotal);
    if (disk != Level::Ok) {
        auto detail = TRANS("health_disk_detail").replace("%1", juce::File::descriptionOfSizeInBytes(in.diskFree));
        if (in.diskTotal > 0) detail = detail.replace("%2", juce::File::descriptionOfSizeInBytes(in.diskTotal));
        add(disk, TRANS(disk == Level::Critical ? "health_disk_full" : "health_disk_low"), detail);
    }

    if (in.backupEnabled) {
        if (in.latestBackup.toMilliseconds() == 0)
            add(Level::Warning, TRANS("health_backup_old"), TRANS("health_backup_never"));
        else if (in.now - in.latestBackup > juce::RelativeTime::hours(backupOverdueHours))
            add(Level::Warning, TRANS("health_backup_old"),
                TRANS("health_backup_last").replace("%1", in.latestBackup.formatted("%d/%m/%Y %H:%M")));
    }
    if (!in.lastBackupOk)
        add(Level::Warning, TRANS("health_backup_failed"), TRANS("health_backup_failed") + ": " + in.lastBackupError);
    if (in.externalConfigured && in.externalAttempted && !in.externalOk)
        add(Level::Warning, TRANS("health_external_failed"), TRANS("health_external_failed") + ": " + in.externalError);

    return alert;
}

}
