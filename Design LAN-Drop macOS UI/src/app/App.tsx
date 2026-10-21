import { useState } from "react";

type Theme = "dark" | "light";
type Tab = "Radar" | "Transfers" | "Storage" | "Settings";

const TABS: Tab[] = ["Radar", "Transfers", "Storage", "Settings"];

const C = {
  dark: {
    bg: "#1E1E1E",
    card: "#2C2C2E",
    border: "#3A3A3C",
    text: "#F5F5F7",
    textSec: "#98989D",
    textTer: "#636366",
    accent: "#0A84FF",
    success: "#30D158",
    danger: "#FF453A",
    warn: "#FFD60A",
    segBg: "#3A3A3C",
    segActive: "#1C1C1E",
    inputBg: "#3A3A3C",
    statusBg: "#0D2B18",
    statusText: "#30D158",
    headerBg: "#252527",
    pillCloud: "#0D2040",
    pillCloudText: "#0A84FF",
    pillLocal: "#3A3A3C",
    pillLocalText: "#98989D",
    successPill: "#0D2B18",
    accentPill: "#0D2040",
    warnPill: "#2C2A18",
    warnPillText: "#FFD60A",
    progressTrack: "#3A3A3C",
    dangerBg: "#3A1515",
  },
  light: {
    bg: "#F5F5F7",
    card: "#FFFFFF",
    border: "#D2D2D7",
    text: "#1D1D1F",
    textSec: "#6E6E73",
    textTer: "#8E8E93",
    accent: "#007AFF",
    success: "#34C759",
    danger: "#FF3B30",
    warn: "#FFCC00",
    segBg: "#E0E0E5",
    segActive: "#FFFFFF",
    inputBg: "#F0F0F2",
    statusBg: "#D4F0DA",
    statusText: "#1A7A36",
    headerBg: "#F8F8FA",
    pillCloud: "#E5F0FF",
    pillCloudText: "#007AFF",
    pillLocal: "#F0F0F2",
    pillLocalText: "#6E6E73",
    successPill: "#D4F0DA",
    accentPill: "#E5F0FF",
    warnPill: "#FFF8D4",
    warnPillText: "#916A00",
    progressTrack: "#E0E0E5",
    dangerBg: "#FFE5E5",
  },
} as const;

type Colors = (typeof C)[Theme];

// ── Shared ────────────────────────────────────────────────────────────────────

function SignalBars({ signal, c }: { signal: number; c: Colors }) {
  return (
    <div style={{ display: "flex", alignItems: "flex-end", gap: 3 }}>
      {[1, 2, 3, 4].map((bar) => (
        <div
          key={bar}
          style={{
            width: 4,
            height: 4 + bar * 3,
            borderRadius: 2,
            backgroundColor: bar <= signal ? c.success : c.textTer,
          }}
        />
      ))}
    </div>
  );
}

function Toggle({ on, onChange, c }: { on: boolean; onChange: (v: boolean) => void; c: Colors }) {
  return (
    <button
      onClick={() => onChange(!on)}
      style={{
        width: 44,
        height: 26,
        borderRadius: 13,
        backgroundColor: on ? c.accent : c.textTer,
        border: "none",
        cursor: "pointer",
        position: "relative",
        transition: "background-color 0.2s ease",
        flexShrink: 0,
        padding: 0,
      }}
    >
      <div
        style={{
          position: "absolute",
          top: 3,
          left: on ? 21 : 3,
          width: 20,
          height: 20,
          borderRadius: "50%",
          backgroundColor: "#FFFFFF",
          transition: "left 0.2s ease",
          boxShadow: "0 1px 4px rgba(0,0,0,0.35)",
        }}
      />
    </button>
  );
}

function Btn({
  children,
  color,
  bgColor,
  border,
  onClick,
}: {
  children: React.ReactNode;
  color: string;
  bgColor: string;
  border?: string;
  onClick?: () => void;
}) {
  return (
    <button
      onClick={onClick}
      style={{
        padding: "6px 14px",
        backgroundColor: bgColor,
        color,
        border: border ?? "none",
        borderRadius: 7,
        fontSize: 13,
        fontWeight: 500,
        cursor: "pointer",
        fontFamily: "inherit",
        whiteSpace: "nowrap",
        lineHeight: 1.4,
      }}
    >
      {children}
    </button>
  );
}

// ── Radar Screen ──────────────────────────────────────────────────────────────

const DEVICES = [
  { name: "MacBook Pro", ip: "192.168.1.42", signal: 4, os: "macOS", icon: "💻", type: "local" },
  { name: "ThinkPad X1", ip: "192.168.1.67", signal: 3, os: "Windows", icon: "🖥️", type: "local" },
  { name: "Galaxy S24", ip: "192.168.1.103", signal: 2, os: "Android", icon: "📱", type: "local" },
  { name: "Cloud Relay", ip: "relay.landrop.io", signal: 4, os: "Cloud", icon: "☁️", type: "cloud" },
];

function RadarScreen({ c, theme, isMobile }: { c: Colors; theme: Theme; isMobile?: boolean }) {
  return (
    <div style={{ height: "100%", display: "flex", flexDirection: "column", padding: isMobile ? 12 : 20, gap: isMobile ? 10 : 14, boxSizing: "border-box", overflowY: "auto" }}>
      {/* Status bar */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          backgroundColor: c.card,
          borderRadius: 10,
          padding: isMobile ? "10px 12px" : "12px 16px",
          border: `1px solid ${c.border}`,
          flexShrink: 0,
          flexWrap: isMobile ? "wrap" : "nowrap",
          gap: isMobile ? 8 : 0,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 10, flexWrap: "wrap" }}>
          <div
            style={{
              width: 8,
              height: 8,
              borderRadius: "50%",
              backgroundColor: c.success,
              boxShadow: `0 0 7px ${c.success}`,
            }}
          />
          <span style={{ fontSize: 13, fontWeight: 600, color: c.text }}>Radar Scanner</span>
          <span
            style={{
              fontSize: 11,
              color: c.success,
              fontWeight: 600,
              backgroundColor: c.successPill,
              padding: "2px 9px",
              borderRadius: 10,
            }}
          >
            Active
          </span>
          <span style={{ fontSize: 11, color: c.textSec }}>4 devices found</span>
        </div>
        <Btn color="#FFFFFF" bgColor={c.accent}>↺ Refresh</Btn>
      </div>

      {/* Device grid */}
      <div style={{ display: "grid", gridTemplateColumns: isMobile ? "1fr" : "1fr 1fr", gap: 10, flex: 1 }}>
        {DEVICES.map((device) => (
          <div
            key={device.name}
            style={{
              backgroundColor: c.card,
              borderRadius: 10,
              padding: "14px 16px",
              border: `1px solid ${c.border}`,
              display: "flex",
              flexDirection: "column",
              gap: 10,
            }}
          >
            <div style={{ display: "flex", alignItems: "flex-start", justifyContent: "space-between" }}>
              <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                <div
                  style={{
                    width: 42,
                    height: 42,
                    backgroundColor: theme === "dark" ? "#3A3A3C" : "#F0F0F2",
                    borderRadius: 10,
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                    fontSize: 22,
                    flexShrink: 0,
                  }}
                >
                  {device.icon}
                </div>
                <div>
                  <div style={{ fontSize: 14, fontWeight: 600, color: c.text }}>{device.name}</div>
                  <div
                    style={{
                      fontSize: 11,
                      color: c.textSec,
                      fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
                      marginTop: 2,
                    }}
                  >
                    {device.ip}
                  </div>
                </div>
              </div>
              <SignalBars signal={device.signal} c={c} />
            </div>
            <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between" }}>
              <span
                style={{
                  fontSize: 11,
                  color: device.type === "cloud" ? c.pillCloudText : c.pillLocalText,
                  backgroundColor: device.type === "cloud" ? c.pillCloud : c.pillLocal,
                  padding: "2px 9px",
                  borderRadius: 10,
                  fontWeight: 500,
                }}
              >
                {device.os}
              </span>
              <Btn color="#FFFFFF" bgColor={c.accent}>Send File</Btn>
            </div>
          </div>
        ))}
      </div>

      {/* NFC Quick-Pair */}
      <div
        style={{
          backgroundColor: c.card,
          borderRadius: 10,
          padding: "12px 16px",
          border: `1px solid ${c.border}`,
          display: "flex",
          alignItems: "center",
          gap: 12,
          flexShrink: 0,
        }}
      >
        <div
          style={{
            width: 36,
            height: 36,
            backgroundColor: theme === "dark" ? "#3A3A3C" : "#F0F0F2",
            borderRadius: 8,
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            fontSize: 18,
            flexShrink: 0,
          }}
        >
          📡
        </div>
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontSize: 12, fontWeight: 600, color: c.text, marginBottom: 4 }}>NFC Quick-Pair</div>
          <input
            type="text"
            placeholder="Hold device near NFC sensor to pair..."
            style={{
              display: "block",
              width: "100%",
              padding: "6px 10px",
              backgroundColor: c.inputBg,
              border: `1px solid ${c.border}`,
              borderRadius: 6,
              fontSize: 12,
              color: c.text,
              outline: "none",
              fontFamily: "inherit",
              boxSizing: "border-box",
            }}
          />
        </div>
        <Btn color="#FFFFFF" bgColor={c.accent}>Simulate Touch</Btn>
      </div>
    </div>
  );
}

// ── Transfers Screen ──────────────────────────────────────────────────────────

const TRANSFERS = [
  { id: "T-0042", name: "project-assets.zip", size: "248 MB", progress: 67, speed: "24.5 MB/s", status: "Transferring", dir: "up" },
  { id: "T-0041", name: "backup-2026-10.tar", size: "1.2 GB", progress: 100, speed: "—", status: "Completed", dir: "up" },
  { id: "T-0040", name: "client-presentation.pdf", size: "18.3 MB", progress: 100, speed: "—", status: "Completed", dir: "down" },
  { id: "T-0039", name: "system-logs.tar.gz", size: "342 MB", progress: 34, speed: "12.1 MB/s", status: "Transferring", dir: "down" },
  { id: "T-0038", name: "design-exports.fig", size: "87.4 MB", progress: 100, speed: "—", status: "Completed", dir: "up" },
  { id: "T-0037", name: "raw-footage-edit.mov", size: "4.7 GB", progress: 8, speed: "31.2 MB/s", status: "Transferring", dir: "down" },
];

function TransfersScreen({ c, theme, isMobile }: { c: Colors; theme: Theme; isMobile?: boolean }) {
  return (
    <div style={{ height: "100%", display: "flex", flexDirection: "column", padding: isMobile ? 12 : 20, gap: isMobile ? 10 : 14, boxSizing: "border-box" }}>
      <div
        style={{
          flex: 1,
          backgroundColor: c.card,
          borderRadius: 10,
          border: `1px solid ${c.border}`,
          overflow: "hidden",
          display: "flex",
          flexDirection: "column",
        }}
      >
        {/* Header row for Desktop */}
        {!isMobile && (
          <div
            style={{
              display: "grid",
              gridTemplateColumns: "72px 1fr 68px 146px 88px 110px 32px",
              padding: "10px 16px",
              borderBottom: `1px solid ${c.border}`,
              backgroundColor: c.headerBg,
            }}
          >
            {["Task ID", "File Name", "Size", "Progress", "Speed", "Status", ""].map((h) => (
              <span
                key={h}
                style={{ fontSize: 11, fontWeight: 600, color: c.textSec, textTransform: "uppercase", letterSpacing: "0.04em" }}
              >
                {h}
              </span>
            ))}
          </div>
        )}

        {/* Rows */}
        <div style={{ flex: 1, overflowY: "auto", padding: isMobile ? 10 : 0 }}>
          {TRANSFERS.map((t, i) => (
            isMobile ? (
              <div
                key={t.id}
                style={{
                  backgroundColor: c.inputBg,
                  borderRadius: 8,
                  padding: "10px 12px",
                  marginBottom: 8,
                  border: `1px solid ${c.border}`,
                  display: "flex",
                  flexDirection: "column",
                  gap: 6,
                }}
              >
                <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between" }}>
                  <div style={{ display: "flex", alignItems: "center", gap: 6, overflow: "hidden" }}>
                    <span style={{ fontSize: 14, color: t.dir === "up" ? c.accent : c.success, fontWeight: 700 }}>
                      {t.dir === "up" ? "↑" : "↓"}
                    </span>
                    <span style={{ fontSize: 13, fontWeight: 600, color: c.text, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
                      {t.name}
                    </span>
                  </div>
                  <span
                    style={{
                      fontSize: 10,
                      fontWeight: 600,
                      color: t.status === "Completed" ? c.success : c.accent,
                      backgroundColor: t.status === "Completed" ? c.successPill : c.accentPill,
                      padding: "2px 7px",
                      borderRadius: 10,
                    }}
                  >
                    {t.status}
                  </span>
                </div>
                <div style={{ display: "flex", alignItems: "center", gap: 6 }}>
                  <div style={{ flex: 1, height: 5, backgroundColor: c.progressTrack, borderRadius: 3, overflow: "hidden" }}>
                    <div
                      style={{
                        height: "100%",
                        width: `${t.progress}%`,
                        backgroundColor: t.status === "Completed" ? c.success : c.accent,
                        borderRadius: 3,
                      }}
                    />
                  </div>
                  <span style={{ fontSize: 11, color: c.textSec, minWidth: 26, textAlign: "right", fontFamily: "monospace" }}>
                    {t.progress}%
                  </span>
                </div>
                <div style={{ display: "flex", justifyContent: "space-between", fontSize: 11, color: c.textSec }}>
                  <span>{t.id} · {t.size}</span>
                  <span>{t.speed}</span>
                </div>
              </div>
            ) : (
              <div
                key={t.id}
                style={{
                  display: "grid",
                  gridTemplateColumns: "72px 1fr 68px 146px 88px 110px 32px",
                  padding: "11px 16px",
                  borderBottom: i < TRANSFERS.length - 1 ? `1px solid ${c.border}` : "none",
                  alignItems: "center",
                }}
              >
                <span style={{ fontSize: 11, color: c.textSec, fontFamily: "ui-monospace, SF Mono, Menlo, monospace" }}>
                  {t.id}
                </span>
                <span
                  style={{
                    fontSize: 13,
                    color: c.text,
                    fontWeight: 500,
                    paddingRight: 8,
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                    whiteSpace: "nowrap",
                  }}
                >
                  {t.name}
                </span>
                <span style={{ fontSize: 12, color: c.textSec }}>{t.size}</span>
                <div style={{ display: "flex", alignItems: "center", gap: 6, paddingRight: 6 }}>
                  <div
                    style={{
                      flex: 1,
                      height: 5,
                      backgroundColor: c.progressTrack,
                      borderRadius: 3,
                      overflow: "hidden",
                    }}
                  >
                    <div
                      style={{
                        height: "100%",
                        width: `${t.progress}%`,
                        backgroundColor: t.status === "Completed" ? c.success : c.accent,
                        borderRadius: 3,
                      }}
                    />
                  </div>
                  <span style={{ fontSize: 11, color: c.textSec, minWidth: 26, textAlign: "right", fontFamily: "ui-monospace, SF Mono, Menlo, monospace" }}>
                    {t.progress}%
                  </span>
                </div>
                <span style={{ fontSize: 12, color: c.textSec, fontFamily: "ui-monospace, SF Mono, Menlo, monospace" }}>
                  {t.speed}
                </span>
                <span
                  style={{
                    display: "inline-block",
                    fontSize: 11,
                    fontWeight: 600,
                    color: t.status === "Completed" ? c.success : c.accent,
                    backgroundColor: t.status === "Completed" ? c.successPill : c.accentPill,
                    padding: "3px 8px",
                    borderRadius: 10,
                  }}
                >
                  {t.status}
                </span>
                <span
                  style={{
                    fontSize: 15,
                    color: t.dir === "up" ? c.accent : c.success,
                    textAlign: "center",
                    fontWeight: 700,
                  }}
                >
                  {t.dir === "up" ? "↑" : "↓"}
                </span>
              </div>
            )
          ))}
        </div>
      </div>

      {/* Action bar */}
      <div
        style={{
          backgroundColor: c.card,
          borderRadius: 10,
          padding: "10px 14px",
          border: `1px solid ${c.border}`,
          display: "flex",
          alignItems: "center",
          gap: 6,
          flexShrink: 0,
          overflowX: "auto",
        }}
      >
        <Btn color={c.text} bgColor={c.inputBg} border={`1px solid ${c.border}`}>⏸ Pause</Btn>
        <Btn color={c.text} bgColor={c.inputBg} border={`1px solid ${c.border}`}>▶ Resume</Btn>
        <Btn color={c.danger} bgColor={c.dangerBg} border={`1px solid ${c.danger}44`}>✕ Cancel</Btn>
        <div style={{ flex: 1 }} />
        <Btn color={c.textSec} bgColor="transparent" border={`1px solid ${c.border}`}>Clear</Btn>
      </div>
    </div>
  );
}

// ── Storage Screen ────────────────────────────────────────────────────────────

const FILES = [
  { name: "quarterly-report-Q3.pdf", size: "4.2 MB", chunks: 18, hash: "3a4f9c2e1b7d8a0f…e6c4", icon: "📄" },
  { name: "design-system-v2.fig", size: "127.8 MB", chunks: 512, hash: "f1e2d3c4b5a69708…1a2b", icon: "🎨" },
  { name: "deployment-bundle.tar.gz", size: "892.4 MB", chunks: 3569, hash: "0a1b2c3d4e5f6a7b…cd8e", icon: "📦" },
  { name: "meeting-recordings.mp4", size: "2.1 GB", chunks: 8532, hash: "a9b8c7d6e5f4a3b2…9c1d", icon: "🎬" },
  { name: "client-contracts.zip", size: "38.7 MB", chunks: 155, hash: "4c5d6e7f8a9b0c1d…2e3f", icon: "🗂️" },
];

function StorageScreen({ c, theme, isMobile }: { c: Colors; theme: Theme; isMobile?: boolean }) {
  return (
    <div style={{ height: "100%", display: "flex", flexDirection: "column", padding: isMobile ? 12 : 20, gap: isMobile ? 10 : 14, boxSizing: "border-box", overflowY: "auto" }}>
      {/* Path bar */}
      <div
        style={{
          backgroundColor: c.card,
          borderRadius: 10,
          padding: isMobile ? "10px 12px" : "12px 16px",
          border: `1px solid ${c.border}`,
          display: "flex",
          alignItems: "center",
          gap: 10,
          flexShrink: 0,
        }}
      >
        <span style={{ fontSize: 14, flexShrink: 0 }}>📁</span>
        <div
          style={{
            flex: 1,
            padding: "7px 10px",
            backgroundColor: c.inputBg,
            border: `1px solid ${c.border}`,
            borderRadius: 6,
            fontSize: 12,
            color: c.text,
            fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
            overflow: "hidden",
            textOverflow: "ellipsis",
            whiteSpace: "nowrap",
          }}
        >
          /Users/alex/Documents/LAN-Drop/shared
        </div>
        <Btn color="#FFFFFF" bgColor={c.accent}>↺ Rescan</Btn>
      </div>

      {/* Summary chips */}
      <div style={{ display: "flex", gap: 6, alignItems: "center", flexShrink: 0, flexWrap: "wrap" }}>
        {[
          { label: "5 Files", bg: c.accentPill, color: c.accent },
          { label: "3.16 GB Total", bg: c.pillLocal, color: c.textSec },
          { label: "🔒 PIN Protected", bg: c.successPill, color: c.success },
          { label: "Chunked Transfer", bg: c.warnPill, color: c.warnPillText },
        ].map((chip) => (
          <div
            key={chip.label}
            style={{
              display: "flex",
              alignItems: "center",
              backgroundColor: chip.bg,
              borderRadius: 20,
              padding: "3px 9px",
            }}
          >
            <span style={{ fontSize: 11, fontWeight: 500, color: chip.color }}>{chip.label}</span>
          </div>
        ))}
      </div>

      {/* File table */}
      <div
        style={{
          flex: 1,
          backgroundColor: c.card,
          borderRadius: 10,
          border: `1px solid ${c.border}`,
          overflow: "hidden",
          display: "flex",
          flexDirection: "column",
        }}
      >
        <div
          style={{
            display: "grid",
            gridTemplateColumns: isMobile ? "1fr 70px" : "1fr 80px 74px 1fr",
            padding: "10px 16px",
            borderBottom: `1px solid ${c.border}`,
            backgroundColor: c.headerBg,
          }}
        >
          {(isMobile ? ["File Name", "Size"] : ["File Name", "Size", "Chunks", "SHA-256"]).map((h) => (
            <span
              key={h}
              style={{ fontSize: 11, fontWeight: 600, color: c.textSec, textTransform: "uppercase", letterSpacing: "0.04em" }}
            >
              {h}
            </span>
          ))}
        </div>
        {FILES.map((file, i) => (
          <div
            key={file.name}
            style={{
              display: "grid",
              gridTemplateColumns: isMobile ? "1fr 70px" : "1fr 80px 74px 1fr",
              padding: isMobile ? "8px 12px" : "11px 16px",
              borderBottom: i < FILES.length - 1 ? `1px solid ${c.border}` : "none",
              alignItems: "center",
            }}
          >
            <div style={{ display: "flex", alignItems: "center", gap: 8, overflow: "hidden" }}>
              <span style={{ fontSize: 15, flexShrink: 0 }}>{file.icon}</span>
              <span
                style={{
                  fontSize: 13,
                  color: c.text,
                  fontWeight: 500,
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                  whiteSpace: "nowrap",
                }}
              >
                {file.name}
              </span>
            </div>
            <span style={{ fontSize: 12, color: c.textSec, textAlign: isMobile ? "right" : "left" }}>{file.size}</span>
            {!isMobile && (
              <>
                <span style={{ fontSize: 12, color: c.textSec, fontFamily: "ui-monospace, SF Mono, Menlo, monospace" }}>
                  {file.chunks.toLocaleString()}
                </span>
                <span
                  style={{
                    fontSize: 11,
                    color: c.textTer,
                    fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                    whiteSpace: "nowrap",
                  }}
                >
                  {file.hash}
                </span>
              </>
            )}
          </div>
        ))}
      </div>

      {/* PIN card */}
      <div
        style={{
          backgroundColor: c.card,
          borderRadius: 10,
          padding: isMobile ? "10px 12px" : "12px 16px",
          border: `1px solid ${c.border}`,
          display: "flex",
          alignItems: "center",
          gap: 10,
          flexShrink: 0,
          flexWrap: isMobile ? "wrap" : "nowrap",
        }}
      >
        <div
          style={{
            width: 32,
            height: 32,
            backgroundColor: theme === "dark" ? "#3A3A3C" : "#F0F0F2",
            borderRadius: 8,
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            fontSize: 16,
            flexShrink: 0,
          }}
        >
          🔐
        </div>
        <div style={{ flex: 1, minWidth: 140 }}>
          <div style={{ fontSize: 12, fontWeight: 600, color: c.text }}>Access Protection</div>
          <div style={{ fontSize: 10, color: c.textSec }}>PIN or password required to access storage</div>
        </div>
        <input
          type="password"
          placeholder="PIN"
          style={{
            width: isMobile ? 80 : 110,
            padding: "6px 10px",
            backgroundColor: c.inputBg,
            border: `1px solid ${c.border}`,
            borderRadius: 6,
            fontSize: 12,
            color: c.text,
            outline: "none",
            fontFamily: "inherit",
            letterSpacing: "0.2em",
          }}
        />
        <Btn color="#FFFFFF" bgColor={c.success}>Unlock</Btn>
      </div>
    </div>
  );
}

// ── Settings Screen ───────────────────────────────────────────────────────────

const LOG_LINES = [
  { time: "14:32:01", level: "INFO", msg: "LAN-Drop daemon started on port 8848" },
  { time: "14:32:01", level: "INFO", msg: "Scanning local subnet 192.168.1.0/24..." },
  { time: "14:32:03", level: "INFO", msg: "Discovered: MacBook Pro @ 192.168.1.42" },
  { time: "14:32:03", level: "INFO", msg: "Discovered: ThinkPad X1 @ 192.168.1.67" },
  { time: "14:32:05", level: "INFO", msg: "Discovered: Galaxy S24 @ 192.168.1.103" },
  { time: "14:32:07", level: "INFO", msg: "Cloud Relay connected: relay.landrop.io" },
  { time: "14:33:12", level: "INFO", msg: "Transfer started: T-0042 project-assets.zip → 192.168.1.67" },
  { time: "14:34:55", level: "WARN", msg: "Speed limit active: 25 MB/s throttle applied" },
  { time: "14:35:01", level: "INFO", msg: "NFC subsystem initialized, antenna ready" },
  { time: "14:35:22", level: "INFO", msg: "AirDrop compatibility protocol enabled" },
  { time: "14:36:44", level: "INFO", msg: "SHA-256 verification passed: deployment-bundle.tar.gz" },
  { time: "14:37:11", level: "WARN", msg: "Peer 192.168.1.103 latency spike: 180ms detected" },
];

interface SettingsProps {
  c: Colors;
  port: string;
  setPort: (v: string) => void;
  speedLimit: number;
  setSpeedLimit: (v: number) => void;
  autoAccept: boolean;
  setAutoAccept: (v: boolean) => void;
  airDropCompat: boolean;
  setAirDropCompat: (v: boolean) => void;
}

function SettingsScreen({ c, port, setPort, speedLimit, setSpeedLimit, autoAccept, setAutoAccept, airDropCompat, setAirDropCompat, isMobile }: SettingsProps & { isMobile?: boolean }) {
  return (
    <div style={{ height: "100%", display: "flex", flexDirection: "column", padding: isMobile ? 12 : 20, gap: isMobile ? 10 : 14, boxSizing: "border-box", overflowY: "auto" }}>
      {/* Preferences card */}
      <div
        style={{
          backgroundColor: c.card,
          borderRadius: 10,
          border: `1px solid ${c.border}`,
          overflow: "hidden",
          flexShrink: 0,
        }}
      >
        {/* Listen port */}
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "13px 16px", borderBottom: `1px solid ${c.border}` }}>
          <div>
            <div style={{ fontSize: 13, fontWeight: 600, color: c.text }}>Listen Port</div>
            <div style={{ fontSize: 11, color: c.textSec, marginTop: 1 }}>TCP/UDP port for peer connections</div>
          </div>
          <input
            type="text"
            value={port}
            onChange={(e) => setPort(e.target.value)}
            style={{
              width: 76,
              padding: "6px 10px",
              backgroundColor: c.inputBg,
              border: `1px solid ${c.border}`,
              borderRadius: 6,
              fontSize: 13,
              color: c.text,
              textAlign: "center",
              outline: "none",
              fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
            }}
          />
        </div>

        {/* Speed limit */}
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "13px 16px", borderBottom: `1px solid ${c.border}` }}>
          <div>
            <div style={{ fontSize: 13, fontWeight: 600, color: c.text }}>Speed Limit</div>
            <div style={{ fontSize: 11, color: c.textSec, marginTop: 1 }}>Maximum transfer rate (0 = unlimited)</div>
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <input
              type="range"
              min={0}
              max={50}
              value={speedLimit}
              onChange={(e) => setSpeedLimit(Number(e.target.value))}
              style={{ width: 130, accentColor: c.accent, cursor: "pointer" }}
            />
            <span
              style={{
                fontSize: 12,
                color: c.text,
                fontWeight: 500,
                fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
                minWidth: 76,
                textAlign: "right",
              }}
            >
              {speedLimit === 0 ? "Unlimited" : `${speedLimit} MB/s`}
            </span>
          </div>
        </div>

        {/* Auto-accept */}
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "13px 16px", borderBottom: `1px solid ${c.border}` }}>
          <div>
            <div style={{ fontSize: 13, fontWeight: 600, color: c.text }}>Auto-Accept Transfers</div>
            <div style={{ fontSize: 11, color: c.textSec, marginTop: 1 }}>Accept incoming files without confirmation prompt</div>
          </div>
          <Toggle on={autoAccept} onChange={setAutoAccept} c={c} />
        </div>

        {/* AirDrop compat */}
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "13px 16px" }}>
          <div>
            <div style={{ fontSize: 13, fontWeight: 600, color: c.text }}>AirDrop Compatibility Protocol</div>
            <div style={{ fontSize: 11, color: c.textSec, marginTop: 1 }}>Enable cross-platform AirDrop-style device discovery</div>
          </div>
          <Toggle on={airDropCompat} onChange={setAirDropCompat} c={c} />
        </div>
      </div>

      {/* Terminal console */}
      <div
        style={{
          flex: 1,
          backgroundColor: "#0D0D0F",
          borderRadius: 10,
          border: "1px solid #2A2A2C",
          overflow: "hidden",
          display: "flex",
          flexDirection: "column",
        }}
      >
        <div
          style={{
            padding: "8px 14px",
            borderBottom: "1px solid #2A2A2C",
            backgroundColor: "#151518",
            display: "flex",
            alignItems: "center",
            gap: 8,
          }}
        >
          <div style={{ display: "flex", gap: 6 }}>
            {["#FF5F57", "#FEBC2E", "#28C840"].map((col) => (
              <div key={col} style={{ width: 10, height: 10, borderRadius: "50%", backgroundColor: col }} />
            ))}
          </div>
          <span
            style={{
              fontSize: 11,
              color: "#636366",
              fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
              marginLeft: 6,
            }}
          >
            landrop — diagnostics — 96×28
          </span>
          <div style={{ flex: 1 }} />
          <div
            style={{
              width: 7,
              height: 7,
              borderRadius: "50%",
              backgroundColor: "#30D158",
              boxShadow: "0 0 6px #30D158",
            }}
          />
        </div>
        <div
          style={{
            flex: 1,
            padding: "10px 14px",
            overflowY: "auto",
            fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
          }}
        >
          {LOG_LINES.map((line, i) => (
            <div key={i} style={{ fontSize: 11, lineHeight: "1.8", display: "flex", gap: 6 }}>
              <span style={{ color: "#3A3A3C", flexShrink: 0 }}>[2026-10-21 {line.time}]</span>
              <span
                style={{
                  color: line.level === "WARN" ? "#FFD60A" : line.level === "ERROR" ? "#FF453A" : "#0A84FF",
                  flexShrink: 0,
                  fontWeight: 700,
                  minWidth: 36,
                }}
              >
                {line.level}
              </span>
              <span style={{ color: line.level === "WARN" ? "#FFD60A" : "#98989D" }}>{line.msg}</span>
            </div>
          ))}
          <div style={{ display: "flex", alignItems: "center", marginTop: 2 }}>
            <span style={{ color: "#30D158", fontSize: 11 }}>$&nbsp;</span>
            <span
              style={{
                display: "inline-block",
                width: 7,
                height: 13,
                backgroundColor: "#98989D",
                animation: "blink 1.2s step-end infinite",
              }}
            />
          </div>
        </div>
      </div>
    </div>
  );
}

// ── Title Bar ─────────────────────────────────────────────────────────────────

function TitleBar({
  c,
  theme,
  activeTab,
  setActiveTab,
  onToggleTheme,
}: {
  c: Colors;
  theme: Theme;
  activeTab: Tab;
  setActiveTab: (t: Tab) => void;
  onToggleTheme?: () => void;
}) {
  return (
    <div
      style={{
        height: 52,
        backgroundColor: c.card,
        borderBottom: `1px solid ${c.border}`,
        display: "flex",
        alignItems: "center",
        paddingLeft: 16,
        paddingRight: 16,
        flexShrink: 0,
      }}
    >
      {/* Traffic lights */}
      <div style={{ display: "flex", gap: 8, alignItems: "center", width: 72, flexShrink: 0 }}>
        {(["#FF5F57", "#FEBC2E", "#28C840"] as const).map((color) => (
          <div
            key={color}
            style={{
              width: 12,
              height: 12,
              borderRadius: "50%",
              backgroundColor: color,
              boxShadow: "0 0 0 0.5px rgba(0,0,0,0.18)",
            }}
          />
        ))}
      </div>

      {/* Segmented control */}
      <div style={{ flex: 1, display: "flex", justifyContent: "center" }}>
        <div
          style={{
            display: "flex",
            backgroundColor: c.segBg,
            borderRadius: 8,
            padding: 2,
          }}
        >
          {TABS.map((tab) => (
            <button
              key={tab}
              onClick={() => setActiveTab(tab)}
              style={{
                padding: "5px 18px",
                borderRadius: 6,
                border: "none",
                cursor: "pointer",
                fontSize: 13,
                fontWeight: activeTab === tab ? 600 : 400,
                backgroundColor: activeTab === tab ? c.segActive : "transparent",
                color: activeTab === tab ? c.text : c.textSec,
                boxShadow:
                  activeTab === tab
                    ? theme === "dark"
                      ? "0 1px 4px rgba(0,0,0,0.55)"
                      : "0 1px 3px rgba(0,0,0,0.12)"
                    : "none",
                transition: "all 0.15s ease",
                fontFamily: "inherit",
                lineHeight: 1.4,
              }}
            >
              {tab}
            </button>
          ))}
        </div>
      </div>

      {/* Right: theme label + status */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: 10,
          width: 160,
          justifyContent: "flex-end",
          flexShrink: 0,
        }}
      >
        <button
          onClick={onToggleTheme}
          style={{
            background: "none",
            border: `1px solid ${c.border}`,
            borderRadius: 12,
            padding: "3px 8px",
            fontSize: 11,
            color: c.textSec,
            cursor: onToggleTheme ? "pointer" : "default",
          }}
        >
          {theme === "dark" ? "● Dark" : "○ Light"}
        </button>
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 5,
            backgroundColor: c.statusBg,
            borderRadius: 10,
            padding: "4px 9px",
          }}
        >
          <div style={{ width: 6, height: 6, borderRadius: "50%", backgroundColor: c.success }} />
          <span style={{ fontSize: 11, color: c.statusText, fontWeight: 600 }}>Online</span>
        </div>
      </div>
    </div>
  );
}

// ── LanDrop Window ────────────────────────────────────────────────────────────

function LanDropWindow({ theme, onToggleTheme }: { theme: Theme; onToggleTheme?: () => void }) {
  const [activeTab, setActiveTab] = useState<Tab>("Radar");
  const [port, setPort] = useState("8848");
  const [speedLimit, setSpeedLimit] = useState(25);
  const [autoAccept, setAutoAccept] = useState(false);
  const [airDropCompat, setAirDropCompat] = useState(true);

  const c = C[theme];

  return (
    <div
      style={{
        width: 980,
        height: 680,
        backgroundColor: c.bg,
        borderRadius: 12,
        border: `1px solid ${c.border}`,
        boxShadow:
          theme === "dark"
            ? "0 32px 96px rgba(0,0,0,0.9), 0 0 0 0.5px rgba(255,255,255,0.06)"
            : "0 32px 96px rgba(0,0,0,0.22), 0 0 0 0.5px rgba(0,0,0,0.05)",
        display: "flex",
        flexDirection: "column",
        overflow: "hidden",
        fontFamily: '-apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, sans-serif',
        WebkitFontSmoothing: "antialiased",
      } as React.CSSProperties}
    >
      <TitleBar c={c} theme={theme} activeTab={activeTab} setActiveTab={setActiveTab} onToggleTheme={onToggleTheme} />

      <div style={{ flex: 1, overflow: "hidden" }}>
        {activeTab === "Radar" && <RadarScreen c={c} theme={theme} />}
        {activeTab === "Transfers" && <TransfersScreen c={c} theme={theme} />}
        {activeTab === "Storage" && <StorageScreen c={c} theme={theme} />}
        {activeTab === "Settings" && (
          <SettingsScreen
            c={c}
            port={port}
            setPort={setPort}
            speedLimit={speedLimit}
            setSpeedLimit={setSpeedLimit}
            autoAccept={autoAccept}
            setAutoAccept={setAutoAccept}
            airDropCompat={airDropCompat}
            setAirDropCompat={setAirDropCompat}
          />
        )}
      </div>
    </div>
  );
}

// ── LanDrop Mobile Phone ───────────────────────────────────────────────────────

function LanDropMobilePhone({
  theme,
  onToggleTheme,
}: {
  theme: Theme;
  onToggleTheme: () => void;
}) {
  const [activeTab, setActiveTab] = useState<Tab>("Radar");
  const [port, setPort] = useState("8848");
  const [speedLimit, setSpeedLimit] = useState(25);
  const [autoAccept, setAutoAccept] = useState(false);
  const [airDropCompat, setAirDropCompat] = useState(true);

  const c = C[theme];

  return (
    <div
      style={{
        width: 390,
        height: 780,
        backgroundColor: c.bg,
        borderRadius: 44,
        border: `3px solid ${theme === "dark" ? "#38383A" : "#D1D1D6"}`,
        boxShadow:
          theme === "dark"
            ? "0 30px 80px rgba(0,0,0,0.85), 0 0 0 1px rgba(255,255,255,0.08)"
            : "0 30px 80px rgba(0,0,0,0.2), 0 0 0 1px rgba(0,0,0,0.06)",
        display: "flex",
        flexDirection: "column",
        overflow: "hidden",
        position: "relative",
        fontFamily: '-apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, sans-serif',
      }}
    >
      {/* iOS Top Bar with Dynamic Island */}
      <div
        style={{
          height: 44,
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          padding: "0 24px",
          flexShrink: 0,
          position: "relative",
          backgroundColor: c.card,
        }}
      >
        <span style={{ fontSize: 14, fontWeight: 600, color: c.text }}>9:41</span>
        <div
          style={{
            position: "absolute",
            left: "50%",
            transform: "translateX(-50%)",
            width: 104,
            height: 26,
            backgroundColor: "#000000",
            borderRadius: 14,
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
            padding: "0 8px",
          }}
        >
          <div style={{ width: 8, height: 8, borderRadius: "50%", backgroundColor: c.accent }} />
          <div style={{ width: 8, height: 8, borderRadius: "50%", backgroundColor: "#1C1C1E" }} />
        </div>
        <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 12, color: c.text }}>
          <span>5G</span>
          <span>100%</span>
        </div>
      </div>

      {/* Header */}
      <div
        style={{
          height: 48,
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          padding: "0 16px",
          borderBottom: `1px solid ${c.border}`,
          backgroundColor: c.card,
          flexShrink: 0,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <span style={{ fontSize: 16 }}>📶</span>
          <span style={{ fontSize: 15, fontWeight: 700, color: c.text }}>LAN-Drop</span>
          <span
            style={{
              fontSize: 10,
              color: c.success,
              backgroundColor: c.successPill,
              padding: "1px 6px",
              borderRadius: 8,
              fontWeight: 600,
            }}
          >
            Mobile
          </span>
        </div>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <button
            onClick={onToggleTheme}
            style={{
              background: "none",
              border: `1px solid ${c.border}`,
              borderRadius: 12,
              padding: "3px 8px",
              fontSize: 11,
              color: c.textSec,
              cursor: "pointer",
            }}
          >
            {theme === "dark" ? "● Dark" : "○ Light"}
          </button>
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 4,
              backgroundColor: c.statusBg,
              borderRadius: 10,
              padding: "3px 7px",
            }}
          >
            <div style={{ width: 5, height: 5, borderRadius: "50%", backgroundColor: c.success }} />
            <span style={{ fontSize: 10, color: c.statusText, fontWeight: 600 }}>Online</span>
          </div>
        </div>
      </div>

      {/* Body Screen */}
      <div style={{ flex: 1, overflow: "hidden" }}>
        {activeTab === "Radar" && <RadarScreen c={c} theme={theme} isMobile={true} />}
        {activeTab === "Transfers" && <TransfersScreen c={c} theme={theme} isMobile={true} />}
        {activeTab === "Storage" && <StorageScreen c={c} theme={theme} isMobile={true} />}
        {activeTab === "Settings" && (
          <SettingsScreen
            c={c}
            port={port}
            setPort={setPort}
            speedLimit={speedLimit}
            setSpeedLimit={setSpeedLimit}
            autoAccept={autoAccept}
            setAutoAccept={setAutoAccept}
            airDropCompat={airDropCompat}
            setAirDropCompat={setAirDropCompat}
            isMobile={true}
          />
        )}
      </div>

      {/* iOS Bottom Tab Bar */}
      <div
        style={{
          height: 60,
          display: "flex",
          alignItems: "center",
          justifyContent: "space-around",
          borderTop: `1px solid ${c.border}`,
          backgroundColor: c.card,
          flexShrink: 0,
        }}
      >
        {(
          [
            { id: "Radar" as Tab, label: "Radar", icon: "📡" },
            { id: "Transfers" as Tab, label: "Transfers", icon: "⇄" },
            { id: "Storage" as Tab, label: "Storage", icon: "📁" },
            { id: "Settings" as Tab, label: "Settings", icon: "⚙️" },
          ] as const
        ).map((tab) => {
          const isActive = activeTab === tab.id;
          return (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id)}
              style={{
                background: "none",
                border: "none",
                cursor: "pointer",
                display: "flex",
                flexDirection: "column",
                alignItems: "center",
                gap: 2,
                color: isActive ? c.accent : c.textSec,
                padding: "4px 12px",
              }}
            >
              <span style={{ fontSize: 17 }}>{tab.icon}</span>
              <span style={{ fontSize: 10, fontWeight: isActive ? 600 : 400 }}>{tab.label}</span>
            </button>
          );
        })}
      </div>

      {/* iOS Home Indicator */}
      <div
        style={{
          height: 18,
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          flexShrink: 0,
          backgroundColor: c.card,
        }}
      >
        <div style={{ width: 120, height: 4, borderRadius: 2, backgroundColor: theme === "dark" ? "#555558" : "#B0B0B5" }} />
      </div>
    </div>
  );
}

// ── Scaled window wrapper ─────────────────────────────────────────────────────

function ScaledWindow({ children }: { children: React.ReactNode }) {
  const SCALE = 0.65;
  const W = 980;
  const H = 680;
  return (
    <div style={{ width: W * SCALE, height: H * SCALE, overflow: "hidden", flexShrink: 0 }}>
      <div style={{ transform: `scale(${SCALE})`, transformOrigin: "top left", width: W, height: H }}>
        {children}
      </div>
    </div>
  );
}

// ── App ───────────────────────────────────────────────────────────────────────

export default function App() {
  const [theme, setTheme] = useState<Theme>("dark");
  const [viewMode, setViewMode] = useState<"desktop" | "mobile" | "both">(() => {
    if (typeof window !== "undefined" && window.innerWidth < 768) return "mobile";
    return "desktop";
  });

  const toggleTheme = () => setTheme((t) => (t === "dark" ? "light" : "dark"));

  return (
    <div
      style={{
        width: "100vw",
        minHeight: "100vh",
        backgroundColor: "#080808",
        backgroundImage:
          "radial-gradient(ellipse 70% 55% at 18% 22%, rgba(10,132,255,0.09) 0%, transparent 65%), radial-gradient(ellipse 55% 50% at 80% 75%, rgba(48,209,88,0.06) 0%, transparent 65%)",
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        justifyContent: "center",
        padding: "24px 20px",
        boxSizing: "border-box",
        gap: 16,
      }}
    >
      <style>{`@keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0; } }`}</style>

      {/* App identity & Device View Selector */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          width: "100%",
          maxWidth: 1300,
          flexWrap: "wrap",
          gap: 12,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
          <div
            style={{
              width: 32,
              height: 32,
              borderRadius: 8,
              background: "linear-gradient(140deg, #0A84FF 0%, #30D158 100%)",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              fontSize: 16,
            }}
          >
            📶
          </div>
          <span
            style={{
              fontSize: 18,
              fontWeight: 700,
              color: "#F5F5F7",
              fontFamily: '-apple-system, BlinkMacSystemFont, "SF Pro Display", system-ui, sans-serif',
              letterSpacing: "-0.025em",
            }}
          >
            LAN-Drop
          </span>
          <span
            style={{
              fontSize: 11,
              color: "#636366",
              fontFamily: "ui-monospace, SF Mono, Menlo, monospace",
              backgroundColor: "#1C1C1E",
              padding: "2px 8px",
              borderRadius: 10,
              border: "1px solid #3A3A3C",
            }}
          >
            v2.4.1
          </span>
          <span
            style={{
              fontSize: 11,
              color: "#30D158",
              backgroundColor: "#0D2B18",
              padding: "2px 9px",
              borderRadius: 10,
              fontWeight: 600,
            }}
          >
            P2P + Cloud
          </span>
        </div>

        {/* View Switcher: Desktop | Mobile | Both */}
        <div style={{ display: "flex", backgroundColor: "#1C1C1E", borderRadius: 8, padding: 3, border: "1px solid #3A3A3C" }}>
          {[
            { id: "desktop", label: "🖥️ ПК (macOS)" },
            { id: "mobile", label: "📱 Телефон (Mobile)" },
            { id: "both", label: "✨ Обидва (Figma Showcase)" },
          ].map((mode) => (
            <button
              key={mode.id}
              onClick={() => setViewMode(mode.id as any)}
              style={{
                padding: "6px 14px",
                borderRadius: 6,
                border: "none",
                cursor: "pointer",
                fontSize: 12,
                fontWeight: viewMode === mode.id ? 600 : 400,
                backgroundColor: viewMode === mode.id ? "#0A84FF" : "transparent",
                color: viewMode === mode.id ? "#FFFFFF" : "#98989D",
                transition: "all 0.15s ease",
              }}
            >
              {mode.label}
            </button>
          ))}
        </div>
      </div>

      {/* Screen Presentation Area */}
      {viewMode === "both" && (
        <>
          <div style={{ display: "flex", gap: 24, justifyContent: "center", width: "100%" }}>
            {[
              { label: "Dark Mode", dot: "#5A5A5F" },
              { label: "Light Mode", dot: "#C0C0C8" },
            ].map(({ label, dot }) => (
              <div
                key={label}
                style={{
                  width: 637,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  gap: 7,
                }}
              >
                <div style={{ width: 8, height: 8, borderRadius: "50%", backgroundColor: dot }} />
                <span
                  style={{
                    fontSize: 10,
                    color: "#4A4A4F",
                    fontFamily: '-apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, sans-serif',
                    letterSpacing: "0.08em",
                    textTransform: "uppercase",
                    fontWeight: 700,
                  }}
                >
                  {label}
                </span>
              </div>
            ))}
          </div>

          <div style={{ display: "flex", gap: 24, flexWrap: "wrap", justifyContent: "center" }}>
            <ScaledWindow>
              <LanDropWindow theme="dark" />
            </ScaledWindow>
            <ScaledWindow>
              <LanDropWindow theme="light" />
            </ScaledWindow>
          </div>
        </>
      )}

      {viewMode === "desktop" && (
        <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 12 }}>
          <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
            <button
              onClick={toggleTheme}
              style={{
                padding: "4px 12px",
                borderRadius: 16,
                border: "1px solid #3A3A3C",
                backgroundColor: "#1C1C1E",
                color: "#F5F5F7",
                fontSize: 12,
                cursor: "pointer",
              }}
            >
              Перемкнути тему: {theme === "dark" ? "● Dark Mode" : "○ Light Mode"}
            </button>
          </div>
          <LanDropWindow theme={theme} onToggleTheme={toggleTheme} />
        </div>
      )}

      {viewMode === "mobile" && (
        <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 12 }}>
          <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
            <button
              onClick={toggleTheme}
              style={{
                padding: "4px 12px",
                borderRadius: 16,
                border: "1px solid #3A3A3C",
                backgroundColor: "#1C1C1E",
                color: "#F5F5F7",
                fontSize: 12,
                cursor: "pointer",
              }}
            >
              Перемкнути тему: {theme === "dark" ? "● Dark Mode" : "○ Light Mode"}
            </button>
          </div>
          <LanDropMobilePhone theme={theme} onToggleTheme={toggleTheme} />
        </div>
      )}

      {/* Footer hint */}
      <p
        style={{
          fontSize: 11,
          color: "#444448",
          fontFamily: '-apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, sans-serif',
          marginTop: 4,
          letterSpacing: "0.01em",
          textAlign: "center",
        }}
      >
        Інтерфейс повністю інтерактивний для ПК та мобільного телефону: перемикання екранів, картки пристроїв, таблиця передач, налаштування та термінал
      </p>
    </div>
  );
}
