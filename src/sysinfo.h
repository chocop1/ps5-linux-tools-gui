#pragma once

#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

namespace ps5 {

struct PS5SystemInfo {
    QString model;
    QString pciId;
    QString kernel;
    QString vbios;
    int vramTotalMb = 0;
    QString sessionType;
};

struct TempEntry {
    bool valid = false;
    double tempC = 0.0;
    QString label;
    bool fault = false;
};

struct SensorSnapshot {
    TempEntry fanTemps[4];
    bool hasFanPwm = false;
    int fanPwm = 0;
    int fanPwmPct = 0;
    int fanPwmEnable = -1;

    bool hasGpuTemp = false;
    double gpuTemp = 0.0;
    bool hasGpuFreq = false;
    int gpuFreqMhz = 0;
    bool hasGpuBusy = false;
    int gpuBusy = 0;
    QString gpuPm = "unknown";
    QString gpuPowerState = "—";
    bool hasVram = false;
    int vramUsedMb = 0;
    int vramTotalMb = 0;
    bool hasGtt = false;
    int gttUsedMb = 0;
    int gttTotalMb = 0;

    bool hasCpuTemp = false;
    double cpuTemp = 0.0;
    bool hasCpuFreq = false;
    int cpuFreqMhz = 0;

    TempEntry nvmeTemps[2];
};

struct FanCurveInfo {
    bool available = false;
    QString hwmonPath;
    int pwmEnable = 2;
    int pwmNow = 0;
    int hystC = 45;
    QVector<QPair<int, int>> points; // (tempC, pwm 0..255)
};

struct DisplayModeEntry {
    QString id;
    QString label;
    int width = 0;
    int height = 0;
    int hz = 0;
    bool active = false;
    bool preferred = false;
};

struct DisplayConnectorInfo {
    QString drmName;
    QString shortName;
    QString status = "unknown";
    QString dpms = "—";
    bool connected = false;
    bool enabled = false;
    QString current = "—";
    QString currentModeId;
    QVector<DisplayModeEntry> modes;
    double scale = 1.0;
    double brightness = 1.0;
    QString vrr = "—";
    QString hdr = "—";
};

struct BluetoothAdapterInfo {
    QString name;
    QString address;
    int softBlocked = -1;
    int hardBlocked = -1;
};

struct WifiAdapterInfo {
    QString iface;
    QString state;
};

struct DisplayAndPeripheralsInfo {
    bool kscreenAvailable = false;
    QVector<DisplayConnectorInfo> connectors;
    QString pcieSpeed = "—";
    QString pcieWidth = "—";
    bool hdcpSupported = false;
    QString vbiosVersion = "—";
    QString resetMethod = "—";
    QMap<QString, QString> fwVersions;
    QVector<BluetoothAdapterInfo> bluetooth;
    QVector<WifiAdapterInfo> wifi;
};

struct BlockDeviceRow {
    QString name;
    QString size;
    QString label;
    QString fstype;
    QString mountpoint;
};

struct M2EnvironmentInfo {
    QVector<BlockDeviceRow> devices;
    bool nvmeDiskExists = false;
    bool nvmePartExists = false;
    bool nvmeIsRoot = false;
    bool imgMounted = false;
    QString imgMountDir = "/mnt/ps5_image_src";
    QString imgMountSrc;
    QMap<QString, bool> efiFiles;
    QString efiVram;
};

PS5SystemInfo detectPS5Info();
SensorSnapshot pollSensors();
FanCurveInfo readFanCurve();
DisplayAndPeripheralsInfo pollDisplayAndPeripherals();
M2EnvironmentInfo pollM2Environment();

} // namespace ps5
